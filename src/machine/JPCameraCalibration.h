// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "common/JPLens.h"

#include <j/config/Json.h>

#include <array>
#include <string>

inline namespace jf {

// What jplacer measured about a camera by moving the head by known amounts
// over a mark and watching where the mark went in the picture.
//
// pixel = centre + pxPerMm * (head offset), where the 2x2 pxPerMm carries the
// scale on each axis, the camera's rotation and any mirroring; `centre` is
// where the mark was with the head at the start. That is through a perfect
// lens: the real one bends the picture (lensK1, about lensCentre: a JPLens for
// a picture width x height), and pixels are straightened before use. The
// camera looks at what is in the middle of the picture (the cross). Measured at the height of
// the mark (`z`): another height is another scale, and (with a tilted camera)
// another viewpoint, which the tilt calibration adds.
struct JPCameraCalibration {
    bool                  valid = false;
    std::array<double, 4> pxPerMm{};          // row-major: [dx/dX dx/dY; dy/dX dy/dY]
    double                lensK1 = 0;         // JPLens::k1
    double                lensCentreX = 0, lensCentreY = 0;   // JPLens's centre, pixels
    int                   width = 0, height = 0;   // the picture it was measured on
    double                z = 0;              // height of the surface it was measured on
    double                rmsPx = 0;          // how far the measurements sat from the fit
    std::string           when;               // when it was measured

    // Millimetres for a displacement in the picture from its middle, seen
    // through the lens (straightened first); nothing when the fit is degenerate.
    bool mmForPixels(double dxPx, double dyPx, double& dxMm, double& dyMm) const;
    JPLens lens() const { return JPLens::forPicture(width, height, lensK1, lensCentreX, lensCentreY); }
    double scaleX() const;   // pixels per mm along the machine's X
    double scaleY() const;
    double rotationDeg() const;
    bool   mirrored() const;

    static JPCameraCalibration fromJson(const JJson& j);
    JJson toJson() const;
};

} // inline namespace jf
