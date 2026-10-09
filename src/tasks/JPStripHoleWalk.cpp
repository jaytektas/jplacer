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

JPStripHoleWalk::Walked JPStripHoleWalk::walk(JPJobMachine& machine, JPPipeline& pipeline, double tapeWidthMm,
                                              const JPLocation& firstPart, const JPLocation& reference, const JPLocation& next,
                                              int holesOn, double stepMm, const std::function<void(int, int)>& looking) {
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
