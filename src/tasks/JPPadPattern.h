// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "geometry/JPAffine2D.h"
#include "job/JPPlacement.h"
#include "library/JPFootprint.h"
#include "machine/JPCameraCalibration.h"
#include "vision/JPGrayImage.h"

inline namespace jf {

// The pattern a part's pads make in a picture: each pattern pixel taken back
// through the camera's calibration (the picture taken looking at lookX,
// lookY) to the machine, through the inverse of `board` to the board, and to
// the footprint's own millimetres at the placement's position and `degrees`
// (mirrored for the bottom side, seen through the board), and lit where a pad
// is (2 by 2 samples a pixel). It covers the pads and a margin round them,
// centred on (ex, ey), where the part's centre is expected; (cx, cy) is that
// centre in the pattern's own pixels. False when the board map cannot be
// undone or the footprint has no pads.
class JPPadPattern {
public:
    static bool draw(const JPCameraCalibration& cal, double lookX, double lookY, const JPAffine2D& board,
                     const JPPlacement& p, const JPFootprint& f, double degrees, double ex, double ey, JPGrayImage& out,
                     double& cx, double& cy);
};

} // inline namespace jf
