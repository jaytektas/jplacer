// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "model/JPConfiguration.h"
#include "model/JPFootprint.h"
#include "model/JPJob.h"
#include "model/JPPlacementsHolderLocation.h"

#include <array>
#include <string>
#include <vector>

inline namespace jf {

// What a board scan looks at (JPPeriodicErrorFit): a board's footprints
// whose pads the head camera can find (JPFootprintFinder), each where the
// board's design puts it, where that is on the machine as the board now
// lies, and its pads' outlines there. A footprint is one when nothing is on
// its pads (a placement not placed yet, or a fiducial) and it is no more
// than kMostSizeMm across (in one look, and quick to match). Bare pads are
// copper from the same artwork as the fiducials: a ruler with a mark every
// few millimetres.
class JPBoardScan {
public:
    struct Point {
        std::string id;
        double      designX = 0, designY = 0;   // on the board (mm)
        double      x = 0, y = 0;               // on the machine (mm)
        double      sizeMm = 0;                 // the larger side of the box round its pads
        // Each pad's outline on the machine, from (x, y) (mm): turned as the footprint lies there.
        std::vector<std::vector<std::array<double, 2>>> pads;
    };
    // The points of `location`'s board, in the order its placements are in. None (and why) without a board.
    static std::vector<Point> points(const JPConfiguration& config, const JPJob& job, const JPPlacementsHolderLocation& location,
                                     std::string& why);
    // `footprint`'s pads' outlines (mm), turned `degrees` (against the clock) and mirrored first when
    // `mirrored` (seen from the bottom side); none when it has no pads.
    static std::vector<std::vector<std::array<double, 2>>> padsOf(const JPFootprint& footprint, double degrees, bool mirrored);
    // The larger side of the box round `pads` (mm).
    static double sizeOf(const std::vector<std::vector<std::array<double, 2>>>& pads);
    static constexpr double kMostSizeMm = 8.0;
};

} // inline namespace jf
