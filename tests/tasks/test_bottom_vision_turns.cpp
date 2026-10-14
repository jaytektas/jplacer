// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// Bottom vision's passes, pre-rotated, where each look tells the part's angle only within +-45 degrees: a part seen
// 45 degrees off at every look (a square part seen round, a tip as wide as it behind it: its angle anything) turns
// a further 45 at each, and is refused once the turns add up past 45 (bench, 2026-10-14: placed 90 degrees out); a
// part picked turned 10 degrees, seen so and then true, is taken, turned back the 10.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>
#include <cmath>

#include "tasks/JPBottomVision.h"

using namespace jf;

int main() {
    JPBottomVision::Settings s;
    s.partName = "TSON-10";
    s.preRotate = true;
    s.wantedAngle = 90;
    s.maxVisionPasses = 3;
    s.maxLinearOffsetMm = 0.02;
    s.maxAngularOffset = 0.5;
    std::string why;
    JPBottomVision::Offset out;

    // Seen 44.94 degrees off wherever the nozzle is turned: refused, saying so.
    {
        int looks = 0;
        const JPBottomVision::Look round = [&](const JPLocation&, double expected, int, JPBottomVision::Seen& seen, std::string&) {
            ++looks;
            seen = { 10, 20, expected + 44.94, 3.5, 3.5 };
            return true;
        };
        assert(!JPBottomVision::findOffsets(s, 10, 20, round, out, why));
        assert(looks == 3 && why.find("more than the 45 its angle can be told within") != std::string::npos);
    }

    // Picked turned 10 degrees: seen so, the nozzle turned back, then seen true; taken.
    {
        double partAngle = 100;   // as picked, with the nozzle at 90
        const JPBottomVision::Look picked = [&](const JPLocation& nozzle, double, int, JPBottomVision::Seen& seen, std::string&) {
            seen = { 10, 20, partAngle + (nozzle.rotation() - 90), 3.3, 3.3 };
            return true;
        };
        why.clear();
        assert(JPBottomVision::findOffsets(s, 10, 20, picked, out, why));
        assert(out.preRotated && std::abs(out.location.rotation() - 10) < 1e-6);
    }
    return 0;
}
