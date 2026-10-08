// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// The picture a vision pipeline is given (JPStraightPicture): taken through a barrel lens bending about a point off
// the middle, by a camera turned and a little out of square, it is straightened (the machine square to it, X along
// x, not mirrored looking down), and a mark found in it is placed on the machine through its calibration where it
// is (to a few thousandths of a mm), at another height too, as through the camera's own on the picture as taken.
// One made for a calibration is shared; a picture of another size is refused.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "camera/JPStraightener.h"
#include "pipeline/JPStraightPicture.h"

#include <opencv2/imgproc.hpp>

#include <cmath>

using namespace jf;

int main() {
    JPCameraCalibration c;
    c.valid = true;
    c.pxPerMm = { -25.9, 0.45, 0.40, 26.1 };   // turned about a degree, not quite square
    c.lensK1 = -0.1;
    c.lensCentreX = 670;
    c.lensCentreY = 376;
    c.width = 1280;
    c.height = 720;
    c.z = 0;
    c.secondZ = 5;
    c.secondScale = c.scale() * 1.04;   // 5 mm nearer, 4 % bigger
    const auto s = JPStraightPicture::of(c, false, 0.3);
    assert(s && JPStraightPicture::of(c, false, 0.3) == s);
    const JPCameraCalibration& sc = s->calibration();
    assert(sc.valid && sc.lensK1 == 0 && sc.width == 1280 && sc.height == 720);
    assert(std::abs(sc.pictureTurnDeg()) < 1e-9 && !sc.pictureMirrored());
    assert(std::abs(std::abs(sc.scaleX()) - std::abs(sc.scaleY())) < 1e-9);
    assert(sc.twoHeights() && std::abs(sc.cameraZ() - c.cameraZ()) < 1e-6);

    // A white mark at (103, 48.5), the camera looking at (100, 50): drawn where the lens puts it, straightened,
    // found (its brightness's middle), and placed.
    const double viewX = 100, viewY = 50, markX = 103, markY = 48.5;
    double px, py;
    assert(c.pixelFor(markX, markY, viewX, viewY, px, py));
    cv::Mat taken(720, 1280, CV_8UC3, cv::Scalar::all(0));
    constexpr int kShift = 4;   // drawn to a sixteenth of a pixel
    cv::circle(taken, cv::Point(int(std::lround(px * (1 << kShift))), int(std::lround(py * (1 << kShift)))), 8 << kShift,
               cv::Scalar::all(255), cv::FILLED, cv::LINE_AA, kShift);
    cv::Mat straight;
    assert(s->straighten(taken, straight) && straight.size() == taken.size());
    cv::Mat gray;
    cv::cvtColor(straight, gray, cv::COLOR_BGR2GRAY);
    const cv::Moments m = cv::moments(gray);
    assert(m.m00 > 0);
    const double sx = m.m10 / m.m00, sy = m.m01 / m.m00;
    double fx, fy;
    assert(sc.machinePoint(sx, sy, viewX, viewY, fx, fy));
    assert(std::abs(fx - markX) < 0.005 && std::abs(fy - markY) < 0.005);
    // Where the pipeline is told to look for it: where it is found.
    double ex, ey;
    assert(sc.pixelFor(markX, markY, viewX, viewY, ex, ey) && std::hypot(ex - sx, ey - sy) < 0.15);

    // At another height, the same through either: the straightened pixel for the one as taken.
    const auto st = JPStraightener::make(c, false, 0.3);
    assert(st);
    double tx, ty, rawMx, rawMy, straightMx, straightMy;
    assert(st->toStraight(px, py, tx, ty));
    assert(c.atHeight(3).machinePoint(px, py, viewX, viewY, rawMx, rawMy));
    assert(sc.atHeight(3).machinePoint(tx, ty, viewX, viewY, straightMx, straightMy));
    assert(std::abs(rawMx - straightMx) < 1e-6 && std::abs(rawMy - straightMy) < 1e-6);

    // A picture of another size is not the calibration's.
    cv::Mat small(360, 640, CV_8UC3, cv::Scalar::all(0)), out;
    assert(!s->straighten(small, out));
    return 0;
}
