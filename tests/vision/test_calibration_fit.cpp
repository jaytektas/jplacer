// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// The camera calibration's fit: a known transform is recovered from noisy
// samples, and bench's real nudges over its homing mark (the head moved by
// known amounts, the mark found by JPRoundMarkFinder) give its top camera's
// scale. Too few or collinear samples give no fit.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "common/JPLens.h"
#include "machine/JPCameraCalibration.h"
#include "vision/JPCalibrationFit.h"

#include <cmath>
#include <random>

using namespace jf;

int main() {
    // Hidden: 25 px/mm, turned 1.5 degrees, looking down unmirrored: the
    // aligned camera [-s 0; 0 s] turned by the angle.
    const double s = 25, a = 1.5 / 57.29577951308232;
    const double M[4] = { -s * std::cos(a), -s * std::sin(a), -s * std::sin(a), s * std::cos(a) };
    std::mt19937 rng(11);
    std::normal_distribution<double> noise(0, 0.1);
    std::vector<JPCalibrationFit::Sample> samples;
    for (int iy = -1; iy <= 1; ++iy)
        for (int ix = -1; ix <= 1; ++ix) {
            const double dx = 2.0 * ix, dy = 2.0 * iy;
            samples.push_back({ dx, dy, 640 + M[0] * dx + M[1] * dy + noise(rng), 360 + M[2] * dx + M[3] * dy + noise(rng) });
        }
    const auto r = JPCalibrationFit::fit(samples);
    assert(r && std::abs(r->centreX - 640) < 0.1 && std::abs(r->centreY - 360) < 0.1 && r->rmsPx < 0.2);
    for (int i = 0; i < 4; ++i) assert(std::abs(r->pxPerMm[i] - M[i]) < 0.05);
    JPCameraCalibration cal;
    cal.valid = true;
    cal.pxPerMm = r->pxPerMm;
    assert(std::abs(cal.scaleX() - 25) < 0.05 && std::abs(cal.scaleY() - 25) < 0.05);
    assert(std::abs(cal.rotationDeg() - 1.5) < 0.1 && !cal.mirrored());
    double mx, my;
    assert(cal.mmForPixels(M[0] * 1 + M[1] * 2, M[2] * 1 + M[3] * 2, mx, my));   // the picture shift of a (1, 2) mm move
    assert(std::abs(mx - 1) < 0.01 && std::abs(my - 2) < 0.01);

    // Bench, 2026-10-03: its top camera over the homing mark, nudged in X then Y.
    const std::vector<JPCalibrationFit::Sample> bench = {
        { 0.0, 0.0, 638.679, 361.110 }, { 0.5, 0.0, 625.957, 361.065 }, { 1.0, 0.0, 613.088, 361.142 },
        { 0.0, 0.0, 638.401, 361.032 }, { 0.0, 0.5, 638.421, 372.814 }, { 0.0, 1.0, 638.487, 385.601 },
        { 0.0, 0.0, 638.353, 361.132 } };
    const auto b = JPCalibrationFit::fit(bench);
    assert(b && b->rmsPx < 0.3);
    JPCameraCalibration top;
    top.valid = true;
    top.pxPerMm = b->pxPerMm;
    assert(std::abs(top.scaleX() - 25.34) < 0.05 && std::abs(top.scaleY() - 24.38) < 0.05);   // least squares over all seven
    assert(!top.mirrored() && std::abs(top.rotationDeg()) < 1);

    // Through a lens: a 5 x 5 grid seen by a 1280 x 720 camera whose lens pulls
    // the edges in. The fit with the lens finds it and everything else exactly;
    // the straight fit cannot.
    {
        const double L[4] = { -25.6, 0.02, 0.01, 25.55 }, k1 = -0.107, c0x = 639.3, c0y = 360.1;
        // Bench's lens bends about a point off the picture's middle.
        const JPLens lens = JPLens::forPicture(1280, 720, k1, 672, 373);
        std::vector<JPCalibrationFit::Sample> grid;
        for (int iy = -2; iy <= 2; ++iy)
            for (int ix = -2; ix <= 2; ++ix) {
                const double dx = ix * 5.6, dy = iy * 5.6;
                double sx, sy;
                lens.distort(c0x + L[0] * dx + L[1] * dy, c0y + L[2] * dx + L[3] * dy, sx, sy);
                grid.push_back({ dx, dy, sx, sy });
            }
        const auto straight = JPCalibrationFit::fit(grid);
        assert(straight && straight->rmsPx > 1);
        const auto bent = JPCalibrationFit::fitWithLens(grid, 1280, 720, true);
        assert(bent && bent->rmsPx < 1e-6 && std::abs(bent->lensK1 - k1) < 1e-6);
        for (int i = 0; i < 4; ++i) assert(std::abs(bent->pxPerMm[i] - L[i]) < 1e-6);
        assert(std::abs(bent->centreX - c0x) < 1e-6 && std::abs(bent->centreY - c0y) < 1e-6);
        assert(std::abs(bent->lensCentreX - 672) < 1e-4 && std::abs(bent->lensCentreY - 373) < 1e-4);
        // Held at the picture's middle, the lens cannot fit as well.
        const auto middle = JPCalibrationFit::fitWithLens(grid, 1280, 720, false);
        assert(middle && middle->rmsPx > 0.1);

        // Back from pixels to millimetres, straightened: the corner's mark is
        // its offset away from the middle's.
        JPCameraCalibration withLens;
        withLens.valid = true;
        withLens.pxPerMm = bent->pxPerMm;
        withLens.lensK1 = bent->lensK1;
        withLens.lensCentreX = bent->lensCentreX;
        withLens.lensCentreY = bent->lensCentreY;
        withLens.width = 1280;
        withLens.height = 720;
        const JPCalibrationFit::Sample& corner = grid.front();
        double mx2, my2;
        assert(withLens.mmForPixels(corner.xPx - 640, corner.yPx - 360, mx2, my2));
        double ox, oy;   // the same for the mark with no offset (the grid's middle, as seen)
        const JPCalibrationFit::Sample& middleSample = grid[grid.size() / 2];
        withLens.mmForPixels(middleSample.xPx - 640, middleSample.yPx - 360, ox, oy);
        assert(std::abs((mx2 - ox) - corner.dxMm) < 1e-6 && std::abs((my2 - oy) - corner.dyMm) < 1e-6);
        // And back again: a machine point to its pixel and to the point.
        double bx, by, rx, ry;
        assert(withLens.pixelFor(103.2, 57.9, 100, 60, bx, by));
        assert(withLens.machinePoint(bx, by, 100, 60, rx, ry));
        assert(std::abs(rx - 103.2) < 1e-6 && std::abs(ry - 57.9) < 1e-6);
        // A point far outside the picture is not in it, though the lens's
        // bending, carried that far, folds it back in.
        double fx, fy;
        assert(!withLens.pixelFor(16.4, 35.2, 100, 60, fx, fy));
        // Too few for seven parameters.
        assert(!JPCalibrationFit::fitWithLens({ grid.begin(), grid.begin() + 4 }, 1280, 720, true));
    }

    // Not enough to fit.
    assert(!JPCalibrationFit::fit({ { 0, 0, 1, 1 }, { 1, 0, 2, 1 } }));
    assert(!JPCalibrationFit::fit({ { 0, 0, 1, 1 }, { 1, 0, 2, 1 }, { 2, 0, 3, 1 } }));   // all along X
    // Two heights: a camera 60 mm above the mark with a 1500 px focal
    // length sees 25 px/mm there and 30 px/mm 10 mm nearer; from the two,
    // where it is, its focal length and the scale anywhere.
    {
        JPCameraCalibration two;
        two.valid = true;
        two.pxPerMm = { -25, 0, 0, 25 };
        two.z = -24;
        two.secondZ = -14;
        two.secondScale = 30;
        assert(two.twoHeights());
        assert(std::abs(two.cameraZ() - 36) < 1e-9 && std::abs(two.focalPx() - 1500) < 1e-9);
        assert(std::abs(two.scaleAt(6) - 50) < 1e-9);
        // Taken at another height (OpenPnP's Default Working Plane Z): its scale
        // there, turned as before, the same camera seen from there.
        {
            const JPCameraCalibration at = two.atHeight(6);
            assert(std::abs(at.scale() - 50) < 1e-9 && at.pxPerMm[0] < 0 && at.pxPerMm[3] > 0);
            assert(std::abs(at.cameraZ() - 36) < 1e-9 && std::abs(at.focalPx() - 1500) < 1e-9);
            double dx, dy;
            assert(at.mmForPixels(50, 0, dx, dy) && std::abs(std::hypot(dx, dy) - 1) < 1e-9);
            JPCameraCalibration one = two;
            one.secondScale = 0;   // one height: as it is
            assert(one.atHeight(6).scale() == one.scale());
        }
        // Kept and read back.
        const JPCameraCalibration back = JPCameraCalibration::fromJson(two.toJson());
        assert(back.twoHeights() && std::abs(back.cameraZ() - 36) < 1e-9);
        // Looking up (the camera below): nearer is lower.
        JPCameraCalibration up = two;
        up.z = 10;
        up.secondZ = 12;   // raised 2 mm: further away, smaller
        up.secondScale = 25 * 60.0 / 62.0;
        assert(up.twoHeights() && std::abs(up.cameraZ() + 50) < 1e-9);
        // A scale that does not change tells nothing: the scale stays the one measured.
        JPCameraCalibration flat = two;
        flat.secondScale = 25.001;
        assert(!flat.twoHeights() && flat.scaleAt(0) == 25);
        // Estimate Z Coordinate of Object: a feature at Z 6 (50 px/mm there)
        // seems to move 100 px when the camera moves 2 mm: 4 mm at the height
        // measured, twice the move, so twice the scale, and so at Z 6.
        two.width = 640;
        two.height = 480;
        double z = 0;
        std::string why;
        assert(two.estimateObjectZ(300, 240, 400, 240, 2, 0, z, why) && std::abs(z - 6) < 1e-9);
        // At the height measured, as far as it moved.
        assert(two.estimateObjectZ(320, 200, 320, 250, 0, 2, z, why) && std::abs(z + 24) < 1e-9);
        // Under a fixed camera looking up, the nearer the higher scale is lower down.
        up.width = 640;
        up.height = 480;
        assert(up.estimateObjectZ(320, 240, 345, 240, 1, 0, z, why) && std::abs(z - 10) < 1e-9);
        // Without two heights, or without a move, nothing.
        flat.width = 640;
        flat.height = 480;
        assert(!flat.estimateObjectZ(300, 240, 400, 240, 2, 0, z, why) && why.find("Secondary") != std::string::npos);
        assert(!two.estimateObjectZ(300, 240, 400, 240, 0, 0, z, why) && why.find("Actual change") != std::string::npos);
        assert(!two.estimateObjectZ(300, 240, 300, 240, 2, 0, z, why) && why.find("Apparent change") != std::string::npos);
    }
    return 0;
}
