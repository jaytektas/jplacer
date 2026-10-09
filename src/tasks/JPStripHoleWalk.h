// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "model/JPLocation.h"
#include "tasks/JPJobMachine.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

inline namespace jf {

class JPPipeline;

// Strip Auto Setup's walk down the tape. The strip's reference and next holes (by the two parts clicked) give
// the line its parts are picked along; from holes 4 mm apart, a few hundredths off in one look go into every
// part (OpenPnP stretches the part pitch to how far apart the two are) and turn the tape's angle. So the camera
// follows the holes down the strip to the one its last part sits by, and that is the last hole: the same look's
// error then shared among all the holes between.
class JPStripHoleWalk {
public:
    // The sprocket holes a strip feeder's pipeline found beside the camera's place, on the machine, nearest first.
    static std::vector<JPLocation> holesOf(const JPJobMachine::SeenCircles& seen, double tapeWidthMm);

    // The tape's own scale (pixels a mm in the pipeline's pictures), the camera calibrated at another height: the
    // camera moved `moveMm` either way of `at` along X, then along Y, the holes' moves in the pictures against
    // the machine's. Nothing (and why) when too few holes were seen in both pictures of a move.
    static std::optional<double> scaleAt(JPJobMachine& machine, JPPipeline& pipeline, const JPLocation& at, double moveMm,
                                         std::string& why);
    // `seen` at the scale `pxPerMm` (from scaleAt): its pixels' places on the machine, and its scale.
    static void atScale(JPJobMachine::SeenCircles& seen, double pxPerMm);

    struct Walked {
        JPLocation last { JPLengthUnit::Millimeters };   // the farthest hole found
        int        holes = 0;                            // whole hole pitches from the reference hole to it
        bool       reached = false;                      // it is the one asked for
    };
    // From `reference` and `next` (holes found by the parts clicked), to the hole `holesOn` hole pitches past the
    // reference. Each look from beside the hole as `firstPart` was from the reference hole, the next hole
    // expected along the line through those found so far, `stepMm` at most past the last (half the camera's
    // view along the tape). `looking(hole, of)` is told before each look.
    // `pxPerMm`: the tape's scale (0: the camera's calibrated one).
    static Walked walk(JPJobMachine& machine, JPPipeline& pipeline, double tapeWidthMm, const JPLocation& firstPart,
                       const JPLocation& reference, const JPLocation& next, int holesOn, double stepMm, double pxPerMm,
                       const std::function<void(int hole, int of)>& looking);
};

} // inline namespace jf
