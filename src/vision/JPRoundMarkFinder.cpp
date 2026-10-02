// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPRoundMarkFinder.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <future>
#include <vector>

inline namespace jf {

namespace {

constexpr double kTwoPi = 6.283185307179586;

// Ring sampling: rings out to this multiple of the mark's radius (the mark,
// its edge, and some ground around it), sampled this densely.
constexpr double kRingsTo        = 1.5;
constexpr double kRingStepPx     = 1.0;
constexpr int    kMinRingSamples = 16;
constexpr int    kMaxRingSamples = 64;    // more says no more about how even a ring is
// The mark's size in the coarse copy the search runs on (pixels across).
constexpr double kCoarseDiameter = 6;
// How many of the roundest places found by the coarse search are measured.
constexpr size_t kCandidates = 32;
// Marks whose confidence is within this of the best are as convincing.
constexpr double kAsConvincing = 0.1;
// Where a mark's brightness is compared with its ground's, as shares of its
// radius: well inside it, and just past its edge.
constexpr double kPolarityInside  = 0.5;
constexpr double kPolarityOutside = 1.35;

// Each ring's mean and the spread of its samples about that mean.
struct Ring { double mean = 0, variance = 0; bool ok = false; };

Ring ring(const JPGrayImage& img, double cx, double cy, double r) {
    const int n = std::clamp(int(kTwoPi * r), kMinRingSamples, kMaxRingSamples);
    double sum = 0, sum2 = 0;
    int count = 0;
    for (int i = 0; i < n; ++i) {
        const double a = kTwoPi * i / n;
        float v;
        if (!img.sample(float(cx + r * std::cos(a)), float(cy + r * std::sin(a)), v)) continue;
        sum += v;
        sum2 += double(v) * v;
        ++count;
    }
    Ring out;
    if (count < n * 3 / 4) return out;   // mostly off the picture
    out.mean = sum / count;
    out.variance = std::max(0.0, sum2 / count - out.mean * out.mean);
    out.ok = true;
    return out;
}

// The edge measurement: rays out from the centre found, each finding where
// the brightness changes fastest the way the mark's edge goes (falling going
// out of a bright mark, rising out of a dark one), to a fraction of a pixel;
// a circle is fitted through those points. Only the edge counts, not the
// mark's inside: shiny copper straight under a camera shows the lens's own
// dark reflection in its middle, off to the side it shows the light.
constexpr int    kEdgeRays       = 72;
constexpr double kRayStepPx      = 0.25;
constexpr double kEdgeFrom       = 0.6;   // the stretch of each ray searched, as shares of the radius
constexpr double kEdgeTo         = 1.4;
constexpr double kSlopeSpanPx    = 1.0;   // the brightness change is taken across this
constexpr double kEdgeLevelPx    = 2.0;   // the levels either side of an edge, this far from it
// A ray whose edge is softer than this share of the typical ray's is
// crossing something else (glare, a trace) and is left out.
constexpr double kMinRayContrast = 0.5;
// A point further than this many times the fit's spread from the circle is
// left out and the circle fitted again; the spread is at least this.
constexpr double kOutlierSpread  = 3.0;
constexpr double kMinSpreadPx    = 0.25;
// A ray's edge this near the circle is on it: whichever is more of a pixel
// distance or a share of the radius.
constexpr double kOnCirclePx     = 2.0;
constexpr double kOnCircleShare  = 0.08;

struct Circle {
    double x = 0, y = 0, r = 0;
    double whole = 0;   // the share of all rays whose edge is on the circle (not left out)
    bool ok = false;
};

// Least squares circle through points (x^2 + y^2 + D x + E y + F = 0).
Circle fitCircle(const std::vector<std::pair<double, double>>& pts) {
    Circle c;
    if (pts.size() < 3) return c;
    double mx = 0, my = 0;
    for (const auto& [x, y] : pts) { mx += x; my += y; }
    mx /= double(pts.size());
    my /= double(pts.size());
    // About the points' mean, for a well-conditioned solve.
    double suu = 0, svv = 0, suv = 0, suuu = 0, svvv = 0, suvv = 0, svuu = 0;
    for (const auto& [x, y] : pts) {
        const double u = x - mx, v = y - my;
        suu += u * u; svv += v * v; suv += u * v;
        suuu += u * u * u; svvv += v * v * v; suvv += u * v * v; svuu += v * u * u;
    }
    const double det = suu * svv - suv * suv;
    if (std::abs(det) < 1e-12) return c;
    const double bu = 0.5 * (suuu + suvv), bv = 0.5 * (svvv + svuu);
    const double uc = (bu * svv - bv * suv) / det, vc = (suu * bv - suv * bu) / det;
    c.x = mx + uc;
    c.y = my + vc;
    c.r = std::sqrt(uc * uc + vc * vc + (suu + svv) / double(pts.size()));
    c.ok = true;
    return c;
}

Circle edgeCircle(const JPGrayImage& img, double cx, double cy, double radius, JPRoundMarkFinder::Polarity polarity) {
    struct Ray { double x, y, slope; };
    std::vector<Ray> rays;
    const int span = std::max(1, int(std::lround(kSlopeSpanPx / kRayStepPx / 2)));
    std::vector<float> profile;
    for (int i = 0; i < kEdgeRays; ++i) {
        const double a = kTwoPi * i / kEdgeRays, ux = std::cos(a), uy = std::sin(a);
        const double t0 = kEdgeFrom * radius;
        profile.clear();
        for (double t = t0; t <= kEdgeTo * radius; t += kRayStepPx) {
            float v;
            if (!img.sample(float(cx + t * ux), float(cy + t * uy), v)) break;
            profile.push_back(v);
        }
        // The steepest change of the mark's way, and a parabola through it.
        auto slopeAt = [&](size_t k) {
            const double d = double(profile[k + size_t(span)]) - double(profile[k - size_t(span)]);
            return polarity == JPRoundMarkFinder::Polarity::Bright ? -d
                 : polarity == JPRoundMarkFinder::Polarity::Dark   ? d
                                                                   : std::abs(d);
        };
        if (profile.size() < size_t(4 * span + 3)) continue;
        size_t bestK = 0;
        double best = 0;
        for (size_t k = size_t(span) + 1; k + size_t(span) + 1 < profile.size(); ++k)
            if (const double sl = slopeAt(k); sl > best) { best = sl; bestK = k; }
        if (best <= 0) continue;
        // There, exactly: where the brightness passes halfway between its
        // levels just inside and just outside the edge.
        const int levelAt = int(std::lround(kEdgeLevelPx / kRayStepPx));
        const size_t lo = bestK > size_t(levelAt) ? bestK - size_t(levelAt) : 0;
        const size_t hi = std::min(profile.size() - 1, bestK + size_t(levelAt));
        const double mid = (double(profile[lo]) + double(profile[hi])) / 2;
        double t = -1;
        for (size_t k = lo; k < hi && t < 0; ++k)
            if ((profile[k] - mid) * (profile[k + 1] - mid) <= 0 && profile[k] != profile[k + 1])
                t = t0 + (double(k) + (profile[k] - mid) / (double(profile[k]) - double(profile[k + 1]))) * kRayStepPx;
        if (t < 0) continue;
        rays.push_back({ cx + t * ux, cy + t * uy, best });
    }
    if (rays.size() < kEdgeRays / 2) return {};
    std::vector<double> slopes;
    for (const Ray& r : rays) slopes.push_back(r.slope);
    std::nth_element(slopes.begin(), slopes.begin() + slopes.size() / 2, slopes.end());
    const double typical = slopes[slopes.size() / 2];
    std::vector<std::pair<double, double>> pts;
    for (const Ray& r : rays)
        if (r.slope >= kMinRayContrast * typical) pts.push_back({ r.x, r.y });
    Circle c = fitCircle(pts);
    if (!c.ok) return c;
    double spread = 0;
    for (const auto& [x, y] : pts) spread += std::pow(std::hypot(x - c.x, y - c.y) - c.r, 2);
    spread = std::max(kMinSpreadPx, std::sqrt(spread / double(pts.size())));
    std::vector<std::pair<double, double>> kept;
    for (const auto& p : pts)
        if (std::abs(std::hypot(p.first - c.x, p.second - c.y) - c.r) <= kOutlierSpread * spread) kept.push_back(p);
    c = fitCircle(kept);
    if (!c.ok) return c;
    // On the circle: as near as a real edge is to its circle (a fraction of a
    // pixel to a pixel or two); a "circle" through noise scatters across the
    // whole stretch searched.
    const double onCircle = std::max(kOnCirclePx, kOnCircleShare * c.r);
    int on = 0;
    for (const auto& [x, y] : kept) on += std::abs(std::hypot(x - c.x, y - c.y) - c.r) <= onCircle;
    c.whole = double(on) / kEdgeRays;
    return c;
}

// Whether the middle of a candidate is brighter (or darker) than the ring
// just outside its edge, as `polarity` asks. maxR is kRingsTo radii.
bool polarityMatches(const JPGrayImage& img, double cx, double cy, double maxR, JPRoundMarkFinder::Polarity polarity) {
    if (polarity == JPRoundMarkFinder::Polarity::Either) return true;
    const double r = maxR / kRingsTo;
    const Ring inside = ring(img, cx, cy, kPolarityInside * r);
    const Ring outside = ring(img, cx, cy, kPolarityOutside * r);
    if (!inside.ok || !outside.ok) return false;
    return polarity == JPRoundMarkFinder::Polarity::Bright ? inside.mean > outside.mean : inside.mean < outside.mean;
}

} // namespace

double JPRoundMarkFinder::symmetryAt(const JPGrayImage& image, double cx, double cy, double maxRadius) {
    std::vector<double> means;
    double within = 0;
    for (double r = kRingStepPx; r <= maxRadius; r += kRingStepPx) {
        const Ring rg = ring(image, cx, cy, r);
        if (!rg.ok) return 0;
        means.push_back(rg.mean);
        within += rg.variance;
    }
    if (means.size() < 2) return 0;
    double m = 0;
    for (double v : means) m += v;
    m /= double(means.size());
    double between = 0;
    for (double v : means) between += (v - m) * (v - m);
    between /= double(means.size());
    within /= double(means.size());
    // +1: a perfectly flat patch (no mark) scores nothing, not infinity.
    return between / (within + 1.0);
}

JPRoundMark JPRoundMarkFinder::findAnySize(const JPGrayImage& image, double expectedX, double expectedY,
                                           double searchRadius, double minDiameter, double maxDiameter,
                                           Polarity polarity) {
    constexpr double kSizeStep = 1.2;
    // Each size is a search of its own: they run side by side.
    std::vector<std::future<JPRoundMark>> tries;
    for (double d = minDiameter; d <= maxDiameter; d *= kSizeStep)
        tries.push_back(std::async(std::launch::async, [&image, expectedX, expectedY, searchRadius, d, polarity] {
            Request rq;
            rq.polarity = polarity;
            rq.expectedX = expectedX;
            rq.expectedY = expectedY;
            rq.searchRadius = searchRadius;
            rq.diameter = d;
            return find(image, rq);
        }));
    JPRoundMark best;
    best.why = "nothing round of any size near there";
    for (auto& t : tries) {
        const JPRoundMark m = t.get();
        if (m.found && (!best.found || m.confidence > best.confidence)) best = m;
    }
    return best;
}

JPRoundMark JPRoundMarkFinder::find(const JPGrayImage& image, const Request& rq) {
    JPRoundMark out;
    if (rq.diameter <= 2 || rq.searchRadius < 0) {
        out.why = "the expected size is too small to measure";
        return out;
    }
    const double maxRadius = kRingsTo * rq.diameter / 2;

    // Search where it is cheap, measure where it is exact: the coarse search
    // runs on the picture halved until the mark is about kCoarseDiameter
    // across, every pixel of that, and keeps the roundest few places (a board
    // has holes, vias, pads and reflections as round as any mark); each is
    // then measured on the full picture.
    JPGrayImage coarse;
    const JPGrayImage* level = &image;
    double scale = 1;
    while (rq.diameter / (scale * 2) >= kCoarseDiameter && level->width >= 32 && level->height >= 32) {
        coarse = level->halved();
        level = &coarse;
        scale *= 2;
    }
    struct Spot { double score, x, y; };
    std::vector<Spot> spots;
    {
        const double cx0 = rq.expectedX / scale, cy0 = rq.expectedY / scale, radius = rq.searchRadius / scale;
        for (double dy = -radius; dy <= radius; dy += 1.0)
            for (double dx = -radius; dx <= radius; dx += 1.0) {
                if (dx * dx + dy * dy > radius * radius) continue;
                // Round, and the right way round: a hole is as round as a pad.
                if (!polarityMatches(*level, cx0 + dx, cy0 + dy, maxRadius / scale, rq.polarity)) continue;
                const double sc = symmetryAt(*level, cx0 + dx, cy0 + dy, maxRadius / scale);
                if (sc > 0) spots.push_back({ sc, (cx0 + dx) * scale, (cy0 + dy) * scale });
            }
    }
    std::sort(spots.begin(), spots.end(), [](const Spot& a, const Spot& b) { return a.score > b.score; });
    std::vector<Spot> candidates;   // the roundest, each at least a mark apart
    for (const Spot& sp : spots) {
        bool near = false;
        for (const Spot& c : candidates) near = near || std::hypot(sp.x - c.x, sp.y - c.y) < rq.diameter;
        if (!near) candidates.push_back(sp);
        if (candidates.size() == kCandidates) break;
    }
    if (candidates.empty()) {
        out.why = "nothing round near where the mark should be";
        return out;
    }
    // Of the marks that pass, the most convincing (size and edge closest to
    // what was asked: a round letter beside a fiducial passes, but less
    // well); among those as convincing, the nearest where it was expected (two
    // fiducials may be in view). If none passes, why the roundest did not.
    std::vector<JPRoundMark> found;
    JPRoundMark roundest;
    for (const Spot& c : candidates) {
        JPRoundMark m = measureAt(image, rq, c.x, c.y, scale);
        if (m.found) found.push_back(std::move(m));
        else if (roundest.why.empty()) roundest = std::move(m);
    }
    if (found.empty()) return roundest;
    double top = 0;
    for (const JPRoundMark& m : found) top = std::max(top, m.confidence);
    const JPRoundMark* best = nullptr;
    for (const JPRoundMark& m : found)
        if (m.confidence >= top - kAsConvincing
            && (!best || std::hypot(m.x - rq.expectedX, m.y - rq.expectedY)
                             < std::hypot(best->x - rq.expectedX, best->y - rq.expectedY)))
            best = &m;
    return *best;
}

JPRoundMark JPRoundMarkFinder::measureAt(const JPGrayImage& image, const Request& rq, double x, double y, double scale) {
    JPRoundMark out;
    const double maxRadius = kRingsTo * rq.diameter / 2;
    // The best whole pixel near the candidate: whole, because a grid that
    // followed a fraction would carry it into the answer.
    double fx = std::round(x), fy = std::round(y);
    double score = -1;
    {
        const double cx0 = fx, cy0 = fy, radius = scale + 1;
        for (double dy = -radius; dy <= radius; dy += 1.0)
            for (double dx = -radius; dx <= radius; dx += 1.0) {
                if (dx * dx + dy * dy > radius * radius) continue;
                const double sc = symmetryAt(image, cx0 + dx, cy0 + dy, maxRadius);
                if (sc > score) { score = sc; fx = cx0 + dx; fy = cy0 + dy; }
            }
    }
    if (score <= 0) {
        out.why = "nothing round near where the mark should be";
        return out;
    }

    // Its size, roughly: where ring brightness changes fastest going outward.
    double roughR = 0, steepest = 0, prev = 0;
    bool havePrev = false;
    for (double r = kRingStepPx; r <= maxRadius; r += kRingStepPx / 2) {
        const Ring rg = ring(image, fx, fy, r);
        if (!rg.ok) break;
        if (havePrev) {
            // The mark's way: falling going out of a bright mark, rising out of a dark one.
            const double change = rg.mean - prev;
            const double slope = rq.polarity == Polarity::Bright ? -change
                               : rq.polarity == Polarity::Dark   ? change
                                                                 : std::abs(change);
            if (slope > steepest) { steepest = slope; roughR = r - kRingStepPx / 4; }
        }
        prev = rg.mean;
        havePrev = true;
    }
    out.x = fx;
    out.y = fy;
    out.diameter = 2 * roughR;
    out.symmetry = score;
    auto wrongSize = [&] {
        const double sizeError = out.diameter / rq.diameter - 1;
        out.confidence = std::max(0.0, 1.0 - std::abs(sizeError) / rq.sizeTolerance);
        if (std::abs(sizeError) <= rq.sizeTolerance) return false;
        char buf[160];
        std::snprintf(buf, sizeof buf, "the roundest thing near there measures %.1f px across, not the %.1f px expected",
                      out.diameter, rq.diameter);
        out.why = buf;
        return true;
    };
    if (wrongSize()) return out;

    // Exactly, to a fraction of a pixel: the edge, measured all round, from
    // the best whole pixel and then again from where that put the centre. It
    // must be there nearly all the way round: a mark of that size, not the
    // edge of something bigger or a round thing with a piece missing.
    Circle edge = edgeCircle(image, fx, fy, rq.diameter / 2, rq.polarity);
    if (edge.ok) edge = edgeCircle(image, edge.x, edge.y, edge.r, rq.polarity);
    out.shape = edge.ok ? edge.whole : 0;
    if (!edge.ok || edge.whole < rq.minShape) {
        char buf[160];
        std::snprintf(buf, sizeof buf, "the roundest thing near there is not a whole mark of that size "
                      "(its edge is round for %.0f%% of the way)", out.shape * 100);
        out.why = buf;
        return out;
    }
    out.x = edge.x;
    out.y = edge.y;
    out.diameter = 2 * edge.r;
    if (wrongSize()) return out;
    out.confidence = std::min(out.confidence, out.shape);
    out.found = true;
    return out;
}

} // inline namespace jf
