// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPStripHoleMeasure.h"

#include "tasks/JPStripHoles.h"

#include <cmath>

inline namespace jf {

namespace {

constexpr JPLengthUnit kMm = JPLengthUnit::Millimeters;
// EIA-481's sprocket hole pitch, and half it: a hole found again is the one within this of where it was.
constexpr double kHolePitchMm = 4.0, kHalfHoleMm = 2.0;

} // namespace

std::vector<JPLocation> JPStripHoleMeasure::holesOf(const JPJobMachine::SeenCircles& seen, double tapeWidthMm) {
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

void JPStripHoleMeasure::measure(JPJobMachine& machine, JPPipeline& pipeline, double tapeWidthMm, const JPLocation& firstPart,
                                 int parts, double partPitchMm, JPLocation& ref1, JPLocation& ref2, std::string& note) {
    const double besideX = firstPart.x() - ref1.x(), besideY = firstPart.y() - ref1.y();
    auto measured = [&](const JPLocation& near, JPLocation& hole) {
        const JPLocation from(kMm, near.x() + besideX, near.y() + besideY, 0, 0);
        JPJobMachine::SeenCircles over;
        std::string why;
        if (!machine.positionCamera(from, why) || !machine.seeCircles(from, pipeline, over, why)) return false;
        for (const JPLocation& h : holesOf(over, tapeWidthMm))
            if (std::hypot(h.x() - near.x(), h.y() - near.y()) < kHalfHoleMm) {
                hole = h;
                return true;
            }
        return false;
    };
    const JPLocation guess1 = ref1, guess2 = ref2;
    measured(guess1, ref1);
    measured(guess2, ref2);
    // The holes by the first and last parts: as many whole hole pitches on as the strip's parts reach.
    const long holesOn = parts > 1 ? long(std::floor((parts - 1) * partPitchMm / kHolePitchMm)) : 0;
    const double step = std::hypot(ref2.x() - ref1.x(), ref2.y() - ref1.y());
    if (holesOn <= 1 || step <= 0) return;
    const double along = kHolePitchMm * double(holesOn) / step;
    const JPLocation far(kMm, ref1.x() + (ref2.x() - ref1.x()) * along, ref1.y() + (ref2.y() - ref1.y()) * along, 0, 0);
    JPLocation hole(kMm);
    if (measured(far, hole)) ref2 = hole;
    else
        note = "no hole was found by the strip's last part (" + std::to_string(parts)
               + " parts): the tape's angle and pitch are taken from the next hole, 4 mm on";
}

} // inline namespace jf
