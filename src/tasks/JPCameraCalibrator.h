// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "camera/JPCameraFeed.h"
#include "machine/JPCameraCalibration.h"
#include "machine/JPCell.h"

#include <functional>
#include <optional>
#include <string>

inline namespace jf {

// Measures a head camera with known moves. With the camera roughly over a
// round mark of a known size: find it (at any size: before calibration the
// scale is unknown), make three small moves to learn which way the mark goes,
// then move the head through a grid that carries the mark across the middle
// of the picture, find it after each move, and fit pixel = centre + M offset.
// Every move arrives one-sided (the cell takes up backlash), so the play in
// the drives cannot creep into the scale. The head goes back where it began.
//
// Runs on a thread of its own: it waits on moves and pictures.
class JPCameraCalibrator {
public:
    struct Options {
        double markDiameterMm = 0;     // the mark it looks at
        double markZ = 0;              // the mark's height (the calibration holds there)
        double speed = 0.1;            // share of the axes' rates
        double maxRmsPx = 1.0;         // a worse fit is refused
    };
    // Called before each step, in words ("move 3 of 9").
    using Progress = std::function<void(const std::string&)>;

    static std::optional<JPCameraCalibration> run(JPCell& cell, JPCameraFeed& feed, const Options& options,
                                                  std::string& why, const Progress& progress = nullptr);
};

} // inline namespace jf
