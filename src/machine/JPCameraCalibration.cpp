// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPCameraCalibration.h"

#include <cmath>

inline namespace jf {

namespace {
constexpr double kDegPerRad = 57.29577951308232;
}

bool JPCameraCalibration::mmForPixels(double dxPx, double dyPx, double& dxMm, double& dyMm) const {
    const double a = pxPerMm[0], b = pxPerMm[1], c = pxPerMm[2], d = pxPerMm[3];
    const double det = a * d - b * c;
    if (!valid || std::abs(det) < 1e-12) return false;
    // Both the pixel and the middle of the picture straightened: the
    // displacement a perfect lens would have shown.
    const JPLens l = lens();
    const double mx = width / 2.0, my = height / 2.0;
    double ux, uy, u0x, u0y;
    l.undistort(mx + dxPx, my + dyPx, ux, uy);
    l.undistort(mx, my, u0x, u0y);
    dxMm = ( d * (ux - u0x) - b * (uy - u0y)) / det;
    dyMm = (-c * (ux - u0x) + a * (uy - u0y)) / det;
    return true;
}

double JPCameraCalibration::scaleX() const { return std::hypot(pxPerMm[0], pxPerMm[2]); }
double JPCameraCalibration::scaleY() const { return std::hypot(pxPerMm[1], pxPerMm[3]); }

// Looking down, unmirrored, with its axes along the machine's, a camera sees
// the scene move -x when the head moves +X and +y (down the picture) when it
// moves +Y: pxPerMm = [-s 0; 0 s], determinant negative.

double JPCameraCalibration::rotationDeg() const {
    // How far the machine's X direction is turned from -x in the picture.
    return std::atan2(-pxPerMm[2], -pxPerMm[0]) * kDegPerRad;
}

bool JPCameraCalibration::mirrored() const {
    return pxPerMm[0] * pxPerMm[3] - pxPerMm[1] * pxPerMm[2] > 0;
}

bool JPCameraCalibration::machinePoint(double px, double py, double viewX, double viewY, double& x, double& y) const {
    double dx, dy;
    if (!mmForPixels(px - width / 2.0, py - height / 2.0, dx, dy)) return false;
    x = viewX - dx;
    y = viewY - dy;
    return true;
}

JPCameraCalibration JPCameraCalibration::fromJson(const JJson& j) {
    JPCameraCalibration c;
    c.valid = j["valid"].boolean();
    for (size_t i = 0; i < 4 && i < j["pxPerMm"].size(); ++i) c.pxPerMm[i] = j["pxPerMm"][i].number();
    c.lensK1 = j["lens"]["k1"].number();
    c.lensCentreX = j["lens"]["centreX"].number();
    c.lensCentreY = j["lens"]["centreY"].number();
    c.width  = int(j["picture"]["width"].number());
    c.height = int(j["picture"]["height"].number());
    c.z     = j["z"].number();
    c.rmsPx = j["rmsPx"].number();
    c.when  = j["when"].str();
    return c;
}

JJson JPCameraCalibration::toJson() const {
    JJson j = JJson::object();
    j["valid"] = valid;
    j["pxPerMm"] = JJson::array();
    for (double v : pxPerMm) j["pxPerMm"].push(v);
    j["lens"]["k1"] = lensK1;
    j["lens"]["centreX"] = lensCentreX;
    j["lens"]["centreY"] = lensCentreY;
    j["picture"]["width"]  = width;
    j["picture"]["height"] = height;
    j["z"]     = z;
    j["rmsPx"] = rmsPx;
    j["when"]  = when;
    return j;
}

} // inline namespace jf
