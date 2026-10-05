// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPStripHoles.h"

#include <algorithm>
#include <cmath>

inline namespace jf {

namespace {

// EIA-481 tape's sprocket holes, and OpenPnP's tolerances on them.
constexpr double kHolePitchMm = 4.0, kLeastPitch = 0.9;
// How far a hole may be off the line, and how many pairs the line search tries (OpenPnP's).
constexpr double kHoleLineDistanceMaxMm = 0.5;
constexpr int    kIterations = 100;
// A hole's centre from the tape's edge (1.5 mm holes 1 mm in), and from the
// tape's middle to the part (OpenPnP's getHoleToPartLateral: half the width less 0.5 mm).
constexpr double kEdgeToHoleMm = 1.75, kPartInsetMm = 0.5;

using P = JPRansac::Point;

double dot(const P& a, const P& b) { return a.x * b.x + a.y * b.y; }

// The least hole pitch (EIA-481's 4 mm, less a tenth).
constexpr double kHolePitchMinMm = kHolePitchMm * kLeastPitch;

struct Along {
    JPLocation location;
    double     distance;
};


// The point from the line segment AB (Real Time Collision Detection).
double toSegment(const P& a, const P& b, const P& p) {
    const P ab { b.x - a.x, b.y - a.y }, ap { p.x - a.x, p.y - a.y }, bp { p.x - b.x, p.y - b.y };
    const double e = dot(ap, ab);
    if (e <= 0) return std::sqrt(dot(ap, ap));
    const double f = dot(ab, ab);
    if (e >= f) return std::sqrt(dot(bp, bp));
    return std::sqrt(std::max(0.0, dot(ap, ap) - e * e / f));
}

} // namespace

JPStripHoles::Result JPStripHoles::find(std::vector<Circle> circles, P centre, double px, double tapeWidthMm) {
    Result r;
    // Nearest the camera's centre (over the part) first.
    std::stable_sort(circles.begin(), circles.end(), [&](const Circle& a, const Circle& b) {
        return std::hypot(a.x - centre.x, a.y - centre.y) < std::hypot(b.x - centre.x, b.y - centre.y);
    });
    const double maxToLine = kHoleLineDistanceMaxMm * px;
    const double minDistance = tapeWidthMm * 0.25 * px;
    const double maxDistance = (kEdgeToHoleMm + tapeWidthMm / 2 - kPartInsetMm) * px;
    const double pitch = kHolePitchMm * px, minPitch = kHolePitchMm * kLeastPitch * px;
    std::vector<P> points;
    for (const Circle& c : circles) points.push_back({ c.x, c.y });
    r.lines = JPRansac::lines(points, kIterations, maxToLine, pitch, pitch - minPitch, true);
    // The longest line as far from the part as the holes are (not circles in the part).
    for (const JPRansac::Line& l : r.lines) {
        const double d = toSegment(l.a, l.b, centre);
        if (d >= minDistance && d <= maxDistance) {
            r.best = l;
            r.hasBest = true;
            break;
        }
    }
    if (!r.hasBest) return r;
    std::vector<Circle> onLine;
    for (const Circle& c : circles)
        if (JPRansac::pointToLineDistance(r.best.a, r.best.b, { c.x, c.y }) <= maxToLine) onLine.push_back(c);
    // Each moved onto the line a whole pitch from its end, by the holes' average offset.
    const P a = r.best.a, ab { r.best.b.x - a.x, r.best.b.y - a.y };
    const double len = std::sqrt(dot(ab, ab));
    const P dir { ab.x / len, ab.y / len };
    P total;
    for (const Circle& c : onLine) {
        const double along = dot({ c.x - a.x, c.y - a.y }, dir) / dot(dir, dir);
        const double fitted = std::floor(along / pitch + 0.5) * pitch;
        total.x += a.x + dir.x * fitted - c.x;
        total.y += a.y + dir.y * fitted - c.y;
    }
    const P avg { total.x / double(onLine.size()), total.y / double(onLine.size()) };
    const P fittedA { a.x - avg.x, a.y - avg.y };
    for (const Circle& c : onLine) {
        const double along = dot({ c.x - a.x, c.y - a.y }, dir) / dot(dir, dir);
        const double fitted = std::floor(along / pitch + 0.5) * pitch;
        r.inLine.push_back({ fittedA.x + dir.x * fitted, fittedA.y + dir.y * fitted, c.diameter });
    }
    return r;
}

bool JPStripHoles::referenceHoles(const JPLocation& first, const JPLocation& second, std::vector<JPLocation> holes1,
                                  std::vector<JPLocation> holes2, JPLocation& ref1, JPLocation& ref2, std::string& why) {
    const double fx = second.x() - first.x(), fy = second.y() - first.y();
    holes1.resize(std::min<size_t>(2, holes1.size()));
    holes2.resize(std::min<size_t>(2, holes2.size()));
    std::vector<Along> along;
    for (const JPLocation& h : holes2) along.push_back({ h, fx * (h.x() - first.x()) + fy * (h.y() - first.y()) });
    std::stable_sort(along.begin(), along.end(), [](const Along& a, const Along& b) { return a.distance < b.distance; });
    ref2 = along.back().location;
    std::stable_sort(holes1.begin(), holes1.end(), [&](const JPLocation& a, const JPLocation& b) {
        return std::hypot(a.x() - ref2.x(), a.y() - ref2.y()) < std::hypot(b.x() - ref2.x(), b.y() - ref2.y());
    });
    ref1 = holes1.front();
    if (std::hypot(ref1.x() - ref2.x(), ref1.y() - ref2.y()) < kHolePitchMinMm && holes1.size() > 1) ref1 = holes1[1];
    // Rotated a quarter to the right of the feed direction.
    const double hx = fy, hy = -fx;
    const bool right = hx * (ref1.x() - first.x()) + hy * (ref1.y() - first.y()) > 0
                    && hx * (ref2.x() - first.x()) + hy * (ref2.y() - first.y()) > 0;
    if (!right) {
        why = "The tape is oriented incorrectly for the feed direction of the components selected";
        return false;
    }
    return true;
}

} // inline namespace jf
