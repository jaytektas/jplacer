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

double JPCameraCalibration::scale() const {
    return std::sqrt(std::abs(pxPerMm[0] * pxPerMm[3] - pxPerMm[1] * pxPerMm[2]));
}

// s1 (z1 - C) = s2 (z2 - C) = +-f: the same focal length seen from both heights.
static double centreZ(double s1, double z1, double s2, double z2) { return (s1 * z1 - s2 * z2) / (s1 - s2); }

bool JPCameraCalibration::twoHeights() const {
    const double s1 = scale();
    if (secondScale <= 0 || s1 <= 0 || std::abs(secondZ - z) < 1e-6 || std::abs(secondScale / s1 - 1) < kLeastScaleChange)
        return false;
    const double c = centreZ(s1, z, secondScale, secondZ);
    return (c - z) * (c - secondZ) > 0;
}

double JPCameraCalibration::cameraZ() const {
    return twoHeights() ? centreZ(scale(), z, secondScale, secondZ) : 0;
}

double JPCameraCalibration::focalPx() const {
    return twoHeights() ? scale() * std::abs(z - cameraZ()) : 0;
}

double JPCameraCalibration::scaleAt(double atZ) const {
    if (!twoHeights()) return scale();
    const double d = std::abs(atZ - cameraZ());
    return d > 1e-9 ? focalPx() / d : scale();
}

bool JPCameraCalibration::estimateObjectZ(double px1, double py1, double px2, double py2, double movedX, double movedY,
                                          double& objectZ, std::string& why) const {
    if (!twoHeights()) {
        why = "Secondary Camera Units Per Pixel have not been calibrated.";
        return false;
    }
    const double moved = std::hypot(movedX, movedY);
    if (moved < kLeastMoveMm) {
        why = "Actual change in camera position or actual feature size too small to estimate object Z coordinate.";
        return false;
    }
    // How far it seemed to move, in millimetres at the height measured (through the lens).
    double x1, y1, x2, y2;
    if (!mmForPixels(px1 - width / 2.0, py1 - height / 2.0, x1, y1) || !mmForPixels(px2 - width / 2.0, py2 - height / 2.0, x2, y2)) {
        why = "the camera's calibration cannot place those pixels";
        return false;
    }
    const double seen = std::hypot(x2 - x1, y2 - y1);
    if (seen < kLeastMoveMm) {
        why = "Apparent change in position or apparent size of object feature is too small to estimate object Z "
              "coordinate.";
        return false;
    }
    // Its scale (px/mm), and the distance from the camera that gives it, on the side the heights measured are.
    const double s = scale() * seen / moved;
    objectZ = cameraZ() + (z >= cameraZ() ? 1.0 : -1.0) * focalPx() / s;
    return true;
}

double JPCameraCalibration::scaleX() const { return std::hypot(pxPerMm[0], pxPerMm[2]); }
double JPCameraCalibration::scaleY() const { return std::hypot(pxPerMm[1], pxPerMm[3]); }

// Looking down, unmirrored, with its axes along the machine's, a camera sees
// the scene move -x when the head moves +X and +y (down the picture) when it
// moves +Y: pxPerMm = [-s 0; 0 s], determinant negative. Looking up, the
// same mounting sees the mirror image: [s 0; 0 s], determinant positive.

double JPCameraCalibration::rotationDeg(bool lookingUp) const {
    // How far the machine's X direction is turned from where it is seen straight.
    return lookingUp ? std::atan2(pxPerMm[2], pxPerMm[0]) * kDegPerRad
                     : std::atan2(-pxPerMm[2], -pxPerMm[0]) * kDegPerRad;
}

bool JPCameraCalibration::mirrored(bool lookingUp) const {
    const double det = pxPerMm[0] * pxPerMm[3] - pxPerMm[1] * pxPerMm[2];
    return lookingUp ? det < 0 : det > 0;
}

bool JPCameraCalibration::machinePoint(double px, double py, double viewX, double viewY, double& x, double& y) const {
    double dx, dy;
    if (!mmForPixels(px - width / 2.0, py - height / 2.0, dx, dy)) return false;
    x = viewX - dx;
    y = viewY - dy;
    return true;
}

namespace {

// How near a point must straighten back to where it was bent from (pixels).
constexpr double kRoundTripPx = 0.01;

} // namespace

bool JPCameraCalibration::pixelFor(double x, double y, double viewX, double viewY, double& px, double& py) const {
    if (!valid || width <= 0 || height <= 0) return false;
    // Straightened, the middle of the picture plus M (V - P); then bent by the lens.
    const JPLens l = lens();
    double u0x, u0y;
    l.undistort(width / 2.0, height / 2.0, u0x, u0y);
    const double dx = viewX - x, dy = viewY - y;
    const double ux = u0x + pxPerMm[0] * dx + pxPerMm[1] * dy, uy = u0y + pxPerMm[2] * dx + pxPerMm[3] * dy;
    l.distort(ux, uy, px, py);
    // Far outside the picture the lens's bending folds back on itself and
    // would put a point the camera cannot see into the picture: only a point
    // that straightens back to itself is seen there.
    double bx, by;
    l.undistort(px, py, bx, by);
    return std::hypot(bx - ux, by - uy) < kRoundTripPx;
}

JPCameraCalibration JPCameraCalibration::fromJson(const JJson& j) {
    JPCameraCalibration c;
    c.valid = j["valid"].boolean();
    for (size_t i = 0; i < 4 && i < j["pxPerMm"].size(); ++i) c.pxPerMm[i] = j["pxPerMm"][i].number();
    c.lensK1 = j["lens"]["k1"].number();
    c.lensK2 = j["lens"]["k2"].number();
    c.lensCentreX = j["lens"]["centreX"].number();
    c.lensCentreY = j["lens"]["centreY"].number();
    c.width  = int(j["picture"]["width"].number());
    c.height = int(j["picture"]["height"].number());
    // Measured before the picture's size was kept: its pixels cannot be
    // placed, so it is measured again.
    if (c.width <= 0 || c.height <= 0) c.valid = false;
    c.z     = j["z"].number();
    c.rmsPx = j["rmsPx"].number();
    c.leftOut = int(j["leftOut"].number());
    c.unmeasured = int(j["unmeasured"].number());
    c.when  = j["when"].str();
    for (const JJson& p : j["points"].arr()) {
        Point q;
        q.xPx = p["x"].number();
        q.yPx = p["y"].number();
        q.dxPx = p["dx"].number();
        q.dyPx = p["dy"].number();
        q.leftOut = p["leftOut"].boolean();
        c.points.push_back(q);
    }
    c.outlierPx = j["outlierPx"].number();
    c.secondZ = j["second"]["z"].number();
    c.secondScale = j["second"]["scale"].number();
    c.secondRmsPx = j["second"]["rmsPx"].number();
    return c;
}

JJson JPCameraCalibration::toJson() const {
    JJson j = JJson::object();
    j["valid"] = valid;
    j["pxPerMm"] = JJson::array();
    for (double v : pxPerMm) j["pxPerMm"].push(v);
    j["lens"]["k1"] = lensK1;
    j["lens"]["k2"] = lensK2;
    j["lens"]["centreX"] = lensCentreX;
    j["lens"]["centreY"] = lensCentreY;
    j["picture"]["width"]  = width;
    j["picture"]["height"] = height;
    j["z"]     = z;
    j["rmsPx"] = rmsPx;
    j["leftOut"] = leftOut;
    j["unmeasured"] = unmeasured;
    j["when"]  = when;
    if (!points.empty()) {
        j["points"] = JJson::array();
        for (const Point& p : points) {
            JJson q = JJson::object();
            q["x"] = p.xPx;
            q["y"] = p.yPx;
            q["dx"] = p.dxPx;
            q["dy"] = p.dyPx;
            if (p.leftOut) q["leftOut"] = true;
            j["points"].push(q);
        }
        j["outlierPx"] = outlierPx;
    }
    if (secondScale > 0) {
        j["second"]["z"] = secondZ;
        j["second"]["scale"] = secondScale;
        j["second"]["rmsPx"] = secondRmsPx;
    }
    return j;
}

} // inline namespace jf
