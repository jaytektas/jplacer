// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "camera/JPCameraFeed.h"
#include "machine/JPCameraCalibration.h"
#include "machine/JPCell.h"

#include "vision/JPGrayImage.h"

#include <functional>
#include <optional>
#include <string>

inline namespace jf {

// Measures a camera with known moves: a camera on the head moved over a mark,
// or a mark (a nozzle's tip) moved over a fixed camera. With the two lined up,
// roughly, and a round mark of a known size (or the camera's rough scale): find it (at any size: before calibration the
// scale is unknown), make three small moves to learn which way the mark goes,
// then move the head through a grid that carries the mark across the middle
// of the picture, find it after each move, and fit pixel = centre + M offset
// through a lens that bends the picture (JPLens), fitted with it.
// Every move arrives one-sided (the cell takes up backlash), so the play in
// the drives cannot creep into the scale. The head goes back where it began.
//
// Runs on a thread of its own: it waits on moves and pictures.
class JPCameraCalibrator {
public:
    struct Options {
        double markDiameterMm = 0;     // the mark it looks at; 0: not known (a nozzle's tip)
        double markZ = 0;              // the mark's height (the calibration holds there)
        double speed = 1.0;            // share of the axes' rates (the machine's speed scales it)
        // The grid, the outliers, the worst fit taken (JPCameraConfig::Calibrating).
        JPCameraConfig::Calibrating calibrating;
        // What moves. A camera on the head moves itself over a mark that
        // stays (null). A fixed camera stays, and the mark is carried over it
        // by a tool on the head (a nozzle's tip), already in view and in
        // focus: this is that tool.
        const JPMountConfig* moving = nullptr;
        // Each find (on the calibration's thread): the picture, where the mark was found in it and how big
        // (pixels), and the step ("measuring, move 12 of 38"), for it to be shown where it was found.
        std::function<void(const JPGrayImage& picture, double x, double y, double diameterPx, const std::string& step)> found;
        // Which pass of a calibration made in more than one ("pass 1 of 2"), put before each step; empty: one.
        std::string pass;
    };
    // Called before each step, in words ("move 3 of 9").
    using Progress = std::function<void(const std::string&)>;

    static std::optional<JPCameraCalibration> run(JPCell& cell, JPCameraFeed& feed, const Options& options,
                                                  std::string& why, const Progress& progress = nullptr);
};

} // inline namespace jf
