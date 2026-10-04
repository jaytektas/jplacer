// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "camera/JPCameraFeed.h"
#include "job/JPBoard.h"
#include "job/JPBoardSide.h"
#include "library/JPFootprint.h"
#include "machine/JPCell.h"
#include "machine/JPFiducialConfig.h"

#include <functional>
#include <string>
#include <vector>

inline namespace jf {

// Where a board really is, from its references (the placements marked to
// locate it by, on the side that is up). Given roughly where it is (a guess
// good to a few millimetres and a degree or two), the head camera goes to
// each reference, finds it, centres on it and measures it there (where the
// lens bends nothing), and each find makes the guess better for the next: the
// first moves it, the second turns it, three or more fit it fully (affine),
// which also takes up a machine whose axes are not quite square. The first
// two are looked for widely (until two are found the board's turn is only
// guessed), the rest close by.
//
// Each reference is found by what it is: a fiducial as a round mark (from
// either side of it with a parallax diameter, JPFiducialConfig); a part by its
// pads, drawn from its package at its rotation through the camera's
// calibration (`footprintOf`); else by its look, learned when it was recorded
// by hand. Only its centre is used, so a part whose pads look the same turned
// half round serves as well as any.
//
// fitRecorded fits the positions recorded by hand instead, with no camera.
//
// Runs on a thread of its own: it waits on moves and pictures.
class JPBoardLocator {
public:
    struct Options {
        double fiducialDiameterMm = 1.0;   // where the board does not say (JPPlacement::fiducialMm)
        double firstSearchMm = 10;    // how far from the guess the first two fiducials are looked for
        double searchMm = 2;          // the rest, once the first is found
        double speed = 1.0;           // share of the axes' rates (the machine's speed scales it)
        double maxRmsMm = 0.1;        // fiducials that disagree more are refused
        JPFiducialConfig fiducials;   // passes, how centred, parallax
        // A part's footprint and the angle it is placed at (its package's
        // footprint, JPPlacementRotation); false: none known.
        std::function<bool(const JPPlacement&, JPFootprint&, double& degrees)> footprintOf;
    };
    struct Reference {
        std::string designator;
        bool        found = false;
        double      x = 0, y = 0;     // where it was found, machine
        double      residualMm = 0;   // how far from where the final fit puts it
        std::string why;              // when not found
    };
    struct Result {
        bool                  ok = false;
        JPBoardSide           board;        // the board as found
        std::vector<Reference> references;  // in the order visited
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
    // A first guess by scanning a region (machine X0, Y0 to X1, Y1) for the
    // reference that is a round mark nearest the board's origin, picture by
    // picture, rows from the region's first corner; the first round mark of
    // its size seen is taken to be it. The guess keeps `guess`'s side and
    // turn and is moved to put that reference there; `references` holds it.
    static Result searchStart(JPCell& cell, JPCameraFeed& feed, const JPBoard& board, const JPBoardSide& guess,
                              double x0, double y0, double x1, double y1, const Options& options,
                              const Progress& progress = nullptr);
    // From the positions recorded by hand on the side that is up; `guess`
    // gives the side and, with two, the board's mirror.
    static Result fitRecorded(const JPBoard& board, const JPBoardSide& guess, const Options& options);
};

} // inline namespace jf
