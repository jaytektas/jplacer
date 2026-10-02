// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "camera/JPCameraFeed.h"
#include "job/JPBoard.h"
#include "job/JPBoardSide.h"
#include "machine/JPCell.h"

#include <functional>
#include <string>
#include <vector>

inline namespace jf {

// Where a board really is, from its fiducials. Given roughly where it is (a
// guess good to a few millimetres and a degree or two), the head camera goes
// to each fiducial on the side that is up, finds it, centres on it and
// measures it there (where the lens bends nothing), and each find makes the
// guess better for the next: the first moves it, the second turns it, three or
// more fit it fully (affine), which also takes up a machine whose axes are not
// quite square. The first two are looked for widely (until two are found the
// board's turn is only guessed), the rest close by.
//
// Runs on a thread of its own: it waits on moves and pictures.
class JPBoardLocator {
public:
    struct Options {
        double fiducialDiameterMm = 1.0;   // where the board does not say (JPPlacement::fiducialMm)
        double firstSearchMm = 10;    // how far from the guess the first two fiducials are looked for
        double searchMm = 2;          // the rest, once the first is found
        double speed = 0.1;           // share of the axes' rates
        double maxRmsMm = 0.1;        // fiducials that disagree more are refused
    };
    struct Fiducial {
        std::string designator;
        bool        found = false;
        double      x = 0, y = 0;     // where it was found, machine
        double      residualMm = 0;   // how far from where the final fit puts it
        std::string why;              // when not found
    };
    struct Result {
        bool                  ok = false;
        JPBoardSide           board;        // the board as found
        std::vector<Fiducial> fiducials;    // in the order visited
        bool                  affine = false;   // fitted fully (three or more found), else moved and turned
        double                rmsMm = 0;
        // With a full fit: how far the board's axes, as measured, lean from
        // square (radians; mm of X per mm of Y). A board is square: this is
        // the machine's lean, left over from any squareness correction.
        double                xPerY = 0;
        std::string           why;
    };
    using Progress = std::function<void(const std::string&)>;

    static Result run(JPCell& cell, JPCameraFeed& feed, const JPBoard& board, const JPBoardSide& guess,
                      const Options& options, const Progress& progress = nullptr);
};

} // inline namespace jf
