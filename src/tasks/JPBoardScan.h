// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "model/JPConfiguration.h"
#include "model/JPFootprint.h"
#include "model/JPJob.h"
#include "model/JPPlacementsHolderLocation.h"

#include <optional>
#include <string>
#include <vector>

inline namespace jf {

// What a board scan looks at (JPPeriodicErrorFit): a board's footprints the
// head camera can find the middle of by their symmetry, each where the
// board's design puts it and where that is on the machine as the board now
// lies. A footprint is one when its pads mirror each other through its
// middle, alike (two-pad chips, a fiducial's one pad), it is square to the
// machine (its turn a multiple of 90 degrees), and nothing is on its pads:
// a placement not placed yet, or a fiducial. Bare pads are copper from the
// same artwork as the fiducials: a ruler with a mark every few millimetres.
class JPBoardScan {
public:
    struct Point {
        std::string id;
        double      designX = 0, designY = 0;   // on the board (mm)
        double      x = 0, y = 0;               // on the machine (mm)
        double      sizeMm = 0;                 // the larger side of the box round its pads
    };
    // The points of `location`'s board, in the order its placements are in. None (and why) without a board.
    static std::vector<Point> points(const JPConfiguration& config, const JPJob& job, const JPPlacementsHolderLocation& location,
                                     std::string& why);
    // The larger side of the box round `footprint`'s pads (mm) when they mirror each other through its middle;
    // none when they do not, or it has none.
    static std::optional<double> mirroredSize(const JPFootprint& footprint);
    // Pads this close (mm) are taken to be in the same place, and alike.
    static constexpr double kSameMm = 0.01;
    // A turn this close (degrees) to a multiple of 90 is square.
    static constexpr double kSquareDeg = 2.0;
};

} // inline namespace jf
