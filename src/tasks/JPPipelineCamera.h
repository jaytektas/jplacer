// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "camera/JPCameraFeed.h"
#include "machine/JPCameraCalibration.h"
#include "pipeline/JPPipeline.h"

#include <functional>

inline namespace jf {

// A camera given to a vision pipeline (its Context), as OpenPnP gives its pipelines the camera's corrected picture:
// ImageCapture takes a picture, settled as the stage asks, straightened (JPStraightPicture: the lens's bending out,
// the machine square to it), and the pipeline is told the camera's scale, turn and places in the straightened
// picture's terms. What it finds is placed on the machine through the calibration give() returns.
class JPPipelineCamera {
public:
    // Where the camera looks (x, y), asked when the pipeline wants a machine place in its picture; null: not known.
    using View = std::function<bool(double& x, double& y)>;
    // `feed`, calibrated by `calibration` for its pictures as taken, given to the pipeline; the calibration for the
    // pictures the pipeline is given (straightened). Not calibrated (not valid): its pictures as taken, its scale
    // not known, and that calibration back.
    static JPCameraCalibration give(JPPipeline::Context& context, JPCameraFeed& feed, const JPCameraCalibration& calibration,
                                    View view);
};

} // inline namespace jf
