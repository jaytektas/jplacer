// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "model/JPLocation.h"
#include "tasks/JPJobMachine.h"

#include <string>
#include <vector>

inline namespace jf {

class JPPipeline;

// A strip's reference and last holes as Auto Setup keeps them. The tape's angle and pitch come from the line
// through the two (OpenPnP's ReferenceStripFeeder), so they are measured for it: each hole looked at again from
// the same place beside it (where the first part was clicked, from its hole), so the camera's scale, off at the
// tape's height when calibrated at another, moves both alike; and, with the strip's parts counted, the last hole
// taken by its last part, as far along the tape as it goes, rather than the next one 4 mm on.
class JPStripHoleMeasure {
public:
    // The sprocket holes a strip feeder's pipeline found beside the camera's place, on the machine, nearest first.
    static std::vector<JPLocation> holesOf(const JPJobMachine::SeenCircles& seen, double tapeWidthMm);
    // `ref1`, `ref2`: the holes by the first and second parts clicked, measured again (each left as it was when not
    // found again); `firstPart`: where the first was clicked; `parts`: the strip's (0: not counted);
    // `partPitchMm`: its parts' pitch. `note`: said when the last part's hole was looked for but not found.
    static void measure(JPJobMachine& machine, JPPipeline& pipeline, double tapeWidthMm, const JPLocation& firstPart,
                        int parts, double partPitchMm, JPLocation& ref1, JPLocation& ref2, std::string& note);
};

} // inline namespace jf
