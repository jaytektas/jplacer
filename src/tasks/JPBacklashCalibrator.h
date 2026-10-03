// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "camera/JPCameraFeed.h"
#include "machine/JPCell.h"

#include <functional>
#include <string>

inline namespace jf {

// Measures the play in an axis that carries a camera, and how to make up for
// it: the camera over a round mark, compensation off, the mark found again
// and again as the axis comes in to the same place from either side.
//
//  1. Still: the mark measured several times without moving: how far the
//     measuring itself wanders sets the tolerance (no guessed figure).
//  2. The play against how far the axis comes in from the other side: a
//     short way in, the play is not yet taken up; from far enough, it is all
//     there. The shortest distance that takes all of it is how far a move
//     must sneak up.
//  3. The play against speed, coming in from far at each speed: a drive that
//     overshoots at speed shows less play there.
//  4. A method chosen as OpenPnP chooses it: none when the play is within
//     the tolerance; OneSided when it does not level off within a short
//     sneak-up (it keeps growing with how far the axis came in: only ending
//     every move the same way makes that the same every time); else
//     Directional when it is the same at every speed, DirectionalSneakUp,
//     sneaking up the distance found, when it is not.
//  5. The method tried: moves in from random distances either way (and from
//     afar on each side), each measured against the mean of them all.
//
// Runs on a thread of its own: it waits on moves and pictures. The axis's
// compensation is left as found (the owner keeps the result).
class JPBacklashCalibrator {
public:
    struct Options {
        double markX = 0, markY = 0;     // the mark (machine coordinates)
        double markDiameterMm = 0;
        double speed = 1.0;              // share of the axes' rates for moves between measurements
        double reachMm = 10;             // the furthest a move comes in from in the tests
        double mostSneakUpMm = 0.8;      // longer than this, sneaking up costs too much: one-sided
        double longestLastMm = 2.0;      // one-sided's last stretch at most this long (each move makes it slowly)
        int    still = 6;                // measurements without moving, for the tolerance
        int    frames = 8;               // pictures averaged for each measurement: a single one wanders
                                         // (on bench, 6 µm; 8 together, 2 µm)
        int    tries = 8;                // random moves to try the result
    };
    struct Result {
        bool                   ok = false;
        std::string            why;
        JPAxisConfig::Backlash method = JPAxisConfig::Backlash::None;
        double                 offset = 0, sneakUpMm = 0, speedFactor = 1;
        double                 worstAfterMm = 0;   // the furthest a tried move landed from their mean
        JPBacklashCalibration  data;
    };
    using Progress = std::function<void(const std::string&)>;

    static constexpr double kSpeeds[] = { 0.25, 0.33, 0.5, 0.75, 1.0 };

    static Result run(JPCell& cell, JPCameraFeed& feed, const std::string& axisId, const Options& options,
                      const Progress& progress = nullptr);
};

} // inline namespace jf
