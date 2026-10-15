// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "camera/JPCameraFeed.h"
#include "machine/JPCell.h"

#include <functional>
#include <string>

inline namespace jf {

// Measures the play in an axis that carries a camera, and how to make up for
// it, as OpenPnP's backlash calibration does (CalibrationSolutions), with its
// constants: the camera over a round mark, compensation off while measuring.
//
//  1. Speed control: moving at a lower speed factor must take longer.
//  2. A step test over a millimetre: the steps must land where sent.
//  3. The backlash coming in from a geometric series of distances (each the
//     last times the square root of 2, up to 10 mm, and the full range),
//     from where it stands and wound from 90 mm (or the soft limit) that
//     side first: the largest play within kMaxSneakUpMm, and the shortest
//     distance still showing it, give the sneak-up distance.
//  4. The offset at each speed factor: compensated, in to the mark from
//     90 mm either side, corrected by what is left (damped) up to 4 times.
//  5. The method: longer than mostSneakUpMm a sneak-up, OneSided past the
//     place by the largest play; the play from afar less than the sneak-up
//     needs, DirectionalSneakUp; the same at every speed, Directional (None
//     within the tolerance); else DirectionalSneakUp; an overshoot at the
//     slowest refused.
//  6. The method tried: moves in from random distances either way (and from
//     90 mm on each side), each measured against the mean of them all.
//
// The tolerance is the axis's resolution taken to at least 0.01 mm, or a
// quarter of a pixel when that is coarser. Runs on a thread of its own: it
// waits on moves and pictures. The axis's compensation is left as found (the
// owner keeps the result).
class JPBacklashCalibrator {
public:
    struct Options {
        double markX = 0, markY = 0;     // the mark (machine coordinates)
        double markDiameterMm = 0;
        double speed = 1.0;              // share of the axes' rates for moves between measurements
        double mostSneakUpMm = 0.8;      // OpenPnP's acceptable sneak-up: longer than this, one-sided
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
    // OpenPnP's maxSneakUpOffsetMm: the furthest come-in the largest play is looked for within.
    static constexpr double kMaxSneakUpMm = 2.5;

    static Result run(JPCell& cell, JPCameraFeed& feed, const std::string& axisId, const Options& options,
                      const Progress& progress = nullptr);
};

} // inline namespace jf
