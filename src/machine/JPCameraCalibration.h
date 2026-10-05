// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "common/JPLens.h"

#include <j/config/Json.h>

#include <array>
#include <cmath>
#include <string>
#include <vector>

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
    double                lensK1 = 0, lensK2 = 0;   // JPLens::k1, k2
    double                lensCentreX = 0, lensCentreY = 0;   // JPLens's centre, pixels
    int                   width = 0, height = 0;   // the picture it was measured on
    double                z = 0;              // height of the surface it was measured on
    double                rmsPx = 0;          // how far the measurements sat from the fit
    int                   leftOut = 0;        // measurements left out as far from it
    int                   unmeasured = 0;     // grid places where the mark could not be measured
    std::string           when;               // when it was measured
    // Each measurement, in the order made: where the mark was seen, and how
    // far that is from where the fit puts it (seen less fitted, pixels); one
    // further than `outlierPx` was left out of the fit.
    struct Point {
        double xPx = 0, yPx = 0;
        double dxPx = 0, dyPx = 0;
        bool   leftOut = false;
    };
    std::vector<Point>    points;
    double                outlierPx = 0;
    // Measured again at a second height (`secondZ`, the scale there
    // `secondScale` px/mm, its fit `secondRmsPx`): a camera's scale goes as
    // one over the distance from its centre of projection, so the two give
    // where that is (cameraZ), its focal length, and the scale at any height.
    // 0 when measured at one height only.
    double                secondZ = 0, secondScale = 0, secondRmsPx = 0;
    // Where the picture's middle looked, against where the camera's place
    // says (the mark was put there): mm, at `z` and at `secondZ`; `looked`
    // when measured (an older calibration was not). The two heights apart
    // they give the camera's tilt (OpenPnP's Camera Mounting Error about X
    // and Y): the line it looks along leans by lean() mm across a mm of height.
    bool                  looked = false, secondLooked = false;
    double                lookedX = 0, lookedY = 0, secondLookedX = 0, secondLookedY = 0;
    // What atHeight moved the camera's view by: the lean over the height
    // from the one measured (not kept).
    double                viewShiftX = 0, viewShiftY = 0;

    // The scale at the height measured (px/mm, both ways together).
    double scale() const;
    // Two heights that tell the distance: scales at least kLeastScaleChange
    // apart (the camera then comes out beyond both heights).
    bool   twoHeights() const;
    static constexpr double kLeastScaleChange = 1e-3;
    // With two heights: the camera's centre of projection's Z, its focal
    // length (px), and the scale at height `atZ` (else the one measured).
    double cameraZ() const;
    double focalPx() const;
    double scaleAt(double atZ) const;
    // This calibration for what is at height `atZ`: its scale there, and
    // where it looks there, as the camera leans (with two heights; else as
    // it is).
    JPCameraCalibration atHeight(double atZ) const;
    // Leaning: two heights, where the middle looked measured at both.
    bool   leans() const { return twoHeights() && looked && secondLooked; }
    double leanX() const { return leans() ? (secondLookedX - lookedX) / (secondZ - z) : 0; }
    double leanY() const { return leans() ? (secondLookedY - lookedY) / (secondZ - z) : 0; }
    // OpenPnP's Camera Mounting Error about the machine's X and Y axes
    // (degrees, right hand rule): the camera's axis tipped towards -Y about
    // X, towards X about Y.
    double tiltAboutXDeg() const;
    double tiltAboutYDeg() const;
    // OpenPnP's estimateZCoordinateOfObject: the Z of a feature seen at pixel
    // (px1, py1), then at (px2, py2) after the camera (or, under a fixed
    // camera, the nozzle holding it) moved by (movedX, movedY) mm. How far it
    // seemed to move, against how far it did, is its scale, and so its
    // distance from the camera. False, with why, without two heights or with
    // too small a move.
    bool estimateObjectZ(double px1, double py1, double px2, double py2, double movedX, double movedY, double& objectZ,
                         std::string& why) const;
    // A move (or an apparent one) shorter than this tells nothing (mm).
    static constexpr double kLeastMoveMm = 0.01;

    // Millimetres for a displacement in the picture from its middle, seen
    // through the lens (straightened first); nothing when the fit is degenerate.
    bool mmForPixels(double dxPx, double dyPx, double& dxMm, double& dyMm) const;
    JPLens lens() const { return JPLens::forPicture(width, height, lensK1, lensCentreX, lensCentreY, lensK2); }
    // Where on the machine a thing seen at pixel (px, py) is, for a camera
    // looking at (viewX, viewY): P = V - M^-1 (pixel - middle), straightened.
    bool machinePoint(double px, double py, double viewX, double viewY, double& x, double& y) const;
    // The other way: where in the picture a machine point (x, y) is seen by a
    // camera looking at (viewX, viewY), through the lens.
    bool pixelFor(double x, double y, double viewX, double viewY, double& px, double& py) const;
    double scaleX() const;   // pixels per mm along the machine's X
    double scaleY() const;
    // How far it is turned, and whether it sees the machine mirrored, against
    // the way a camera looking that way sees it when mounted straight: one
    // looking up sees the machine as a mirror image of one looking down.
    double rotationDeg(bool lookingUp = false) const;
    bool   mirrored(bool lookingUp = false) const;

    static JPCameraCalibration fromJson(const JJson& j);
    JJson toJson() const;
};

} // inline namespace jf
