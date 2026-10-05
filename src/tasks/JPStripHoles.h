// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "model/JPLocation.h"
#include "vision/JPRansac.h"

#include <string>

#include <vector>

inline namespace jf {

// OpenPnP's ReferenceStripFeederConfigurationWizard.FindHoles: of the
// circles a strip feeder's pipeline found (pixels), those on the tape's line
// of sprocket holes. The lines through holes a pitch apart, give or take;
// the best: the longest that passes the camera's centre (over a part, not a
// hole) as far from it as the tape puts the holes; its holes moved onto it,
// a whole pitch apart, nearest the camera's centre first.
class JPStripHoles {
public:
    struct Circle {
        double x = 0, y = 0, diameter = 0;
    };
    struct Result {
        std::vector<JPRansac::Line> lines;
        bool                        hasBest = false;
        JPRansac::Line              best;
        std::vector<Circle>         inLine;
    };
    // `centre`: the camera's centre's pixel; `pxPerMm`: its scale; `tapeWidthMm`: the tape's width.
    static Result find(std::vector<Circle> circles, JPRansac::Point centre, double pxPerMm, double tapeWidthMm);
    // OpenPnP's deriveReferenceHoles, for two parts clicked one after the
    // other and the holes found by each (nearest first): of the two nearest
    // each, part 2's farthest along from part 1 to part 2, and part 1's
    // nearest that (the other when it is the same hole: 2 mm pitched tape).
    // False (and why) unless both are right of the feed direction.
    static bool referenceHoles(const JPLocation& first, const JPLocation& second, std::vector<JPLocation> holes1,
                               std::vector<JPLocation> holes2, JPLocation& ref1, JPLocation& ref2, std::string& why);
};

} // inline namespace jf
