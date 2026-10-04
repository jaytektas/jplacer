// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "camera/JPCameraFeed.h"
#include "job/JPBoardSide.h"
#include "job/JPPlacement.h"
#include "library/JPFootprint.h"
#include "machine/JPCell.h"

#include <array>
#include <string>

inline namespace jf {

// Which way round a footprint sits on the bare board, by vision: the head
// camera over the placement (the board located), the footprint's pads drawn
// at the placement's angle and at each further quarter turn (JPPadPattern),
// each matched against the copper (JPPatternFinder). One angle matching
// clearly better than the rest says how many quarter turns the package's 0°
// is out; none does when the pads look alike turned (then the person decides).
//
// Runs on a thread of its own: it waits on a move and a picture.
class JPRotationLook {
public:
    struct Result {
        bool                  ok = false;
        int                   quarters = 0;     // anticlockwise, of the best match
        std::array<double, 4> scores {};        // each quarter's best match, 0..1
        std::string           why;
    };
    static constexpr double kClearMargin = 0.1;   // the best beats the next by this much

    static Result run(JPCell& cell, JPCameraFeed& feed, const JPBoardSide& board, const JPPlacement& p,
                      const JPFootprint& f, double degrees, double speed);
};

} // inline namespace jf
