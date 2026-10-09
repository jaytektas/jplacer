// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPStripHoleWalk.h"

#include "tasks/JPStripHoles.h"

#include <algorithm>
#include <cmath>

inline namespace jf {

namespace {

constexpr JPLengthUnit kMm = JPLengthUnit::Millimeters;
// EIA-481's sprocket hole pitch, and half it: the hole looked for is the one within this of where it should be.
constexpr double kHolePitchMm = 4.0, kHalfHoleMm = 2.0;

} // namespace

std::vector<JPLocation> JPStripHoleWalk::holesOf(const JPJobMachine::SeenCircles& seen, double tapeWidthMm) {
    std::vector<JPStripHoles::Circle> circles;
    for (const auto& c : seen.circles) circles.push_back({ c.x, c.y, c.diameter });
    const JPStripHoles::Result r = JPStripHoles::find(circles, { seen.centreX, seen.centreY }, seen.pixelsPerMm, tapeWidthMm);
    std::vector<JPLocation> out;
    for (const JPStripHoles::Circle& c : r.inLine) {
        double x = 0, y = 0;
        if (seen.toMachine && seen.toMachine(c.x, c.y, x, y)) out.emplace_back(kMm, x, y, 0, 0);
    }
    return out;
}

void JPStripHoleWalk::atScale(JPJobMachine::SeenCircles& seen, double pxPerMm) {
    if (pxPerMm <= 0 || seen.pixelsPerMm <= 0 || !seen.toMachine) return;
    // Offsets from the picture's middle (where the camera looks) taken at the scale the tape is seen at.
    const double k = seen.pixelsPerMm / pxPerMm, cx = seen.centreX, cy = seen.centreY;
    seen.toMachine = [calibrated = seen.toMachine, k, cx, cy](double px, double py, double& x, double& y) {
        double mx, my, ox, oy;
        if (!calibrated(px, py, mx, my) || !calibrated(cx, cy, ox, oy)) return false;
        x = ox + (mx - ox) * k;
        y = oy + (my - oy) * k;
        return true;
    };
    seen.pixelsPerMm = pxPerMm;
}

std::optional<double> JPStripHoleWalk::scaleAt(JPJobMachine& machine, JPPipeline& pipeline, const JPLocation& at, double moveMm,
                                               std::string& why) {
    double pixels = 0, mm = 0;
    int pairs = 0;
    for (const bool alongX : { true, false }) {
        JPJobMachine::SeenCircles seen[2];
        for (int side = 0; side < 2; ++side) {
            const double d = side ? moveMm : -moveMm;
            const JPLocation from(kMm, at.x() + (alongX ? d : 0), at.y() + (alongX ? 0 : d), 0, 0);
            if (!machine.positionCamera(from, why) || !machine.seeCircles(from, pipeline, seen[side], why)) return std::nullopt;
        }
        // Each mark in both pictures: the one in the second nearest where the first puts it on the machine (the same
        // hole: the scale off by a hundredth moves it a fraction of a millimetre, the next hole is a pitch away).
        for (const auto& a : seen[0].circles) {
            double ax, ay;
            if (!seen[0].toMachine(a.x, a.y, ax, ay)) continue;
            const JPJobMachine::SeenCircles::Circle* match = nullptr;
            double nearest = kHalfHoleMm / 2;
            for (const auto& b : seen[1].circles) {
                double bx, by;
                if (!seen[1].toMachine(b.x, b.y, bx, by)) continue;
                if (const double d = std::hypot(bx - ax, by - ay); d < nearest) {
                    nearest = d;
                    match = &b;
                }
            }
            if (!match) continue;
            pixels += std::hypot(match->x - a.x, match->y - a.y);
            mm += 2 * moveMm;
            ++pairs;
        }
    }
    if (pairs < 2 || mm <= 0) {
        why = "too few holes were seen on both sides of the moves to measure the tape's scale";
        return std::nullopt;
    }
    return pixels / mm;
}

JPLocation JPStripHoleWalk::besideHole(const JPLocation& firstPart, const JPLocation& reference, const JPLocation& next, int holesOn) {
    const double dx = next.x() - reference.x(), dy = next.y() - reference.y(), apart = std::hypot(dx, dy);
    const double holes = std::max(1.0, std::round(apart / kHolePitchMm));
    const double along = apart > 0 ? double(holesOn) / holes : 0;
    return JPLocation(kMm, firstPart.x() + dx * along, firstPart.y() + dy * along, 0, 0);
}

JPStripHoleWalk::Walked JPStripHoleWalk::walk(JPJobMachine& machine, JPPipeline& pipeline, double tapeWidthMm,
                                              const JPLocation& firstPart, const JPLocation& reference, const JPLocation& next,
                                              int holesOn, double stepMm, double firstPxPerMm, double lastPxPerMm,
                                              const std::function<void(int, int)>& looking) {
    Walked w;
    w.last = next;
    w.holes = int(std::lround(std::hypot(next.x() - reference.x(), next.y() - reference.y()) / kHolePitchMm));
    if (w.holes < 1) return w;
    w.reached = w.holes >= holesOn;
    const double besideX = firstPart.x() - reference.x(), besideY = firstPart.y() - reference.y();
    const int stride = std::max(1, int(std::floor(stepMm / kHolePitchMm)));
    while (w.holes < holesOn) {
        // Where the hole should be: along the line through the reference and the farthest found, at the pitch they show.
        const double dx = w.last.x() - reference.x(), dy = w.last.y() - reference.y();
        const int to = std::min(holesOn, w.holes + stride);
        const double along = double(to) / double(w.holes);
        const JPLocation expected(kMm, reference.x() + dx * along, reference.y() + dy * along, 0, 0);
        if (looking) looking(to, holesOn);
        const JPLocation from(kMm, expected.x() + besideX, expected.y() + besideY, 0, 0);
        JPJobMachine::SeenCircles seen;
        std::string why;
        if (!machine.positionCamera(from, why) || !machine.seeCircles(from, pipeline, seen, why)) return w;
        atScale(seen, firstPxPerMm > 0 && lastPxPerMm > 0 ? firstPxPerMm + (lastPxPerMm - firstPxPerMm) * to / holesOn : firstPxPerMm);
        const std::vector<JPLocation> holes = holesOf(seen, tapeWidthMm);
        const auto nearest = std::min_element(holes.begin(), holes.end(), [&](const JPLocation& a, const JPLocation& b) {
            return std::hypot(a.x() - expected.x(), a.y() - expected.y()) < std::hypot(b.x() - expected.x(), b.y() - expected.y());
        });
        if (nearest == holes.end() || std::hypot(nearest->x() - expected.x(), nearest->y() - expected.y()) >= kHalfHoleMm) return w;
        w.last = *nearest;
        w.holes = to;
    }
    w.reached = true;
    return w;
}

} // inline namespace jf
