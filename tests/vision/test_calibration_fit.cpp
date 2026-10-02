// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// The camera calibration's fit: a known transform is recovered from noisy
// samples, and bench's real nudges over its homing mark (the head moved by
// known amounts, the mark found by JPRoundMarkFinder) give its top camera's
// scale. Too few or collinear samples give no fit.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

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

    // Not enough to fit.
    assert(!JPCalibrationFit::fit({ { 0, 0, 1, 1 }, { 1, 0, 2, 1 } }));
    assert(!JPCalibrationFit::fit({ { 0, 0, 1, 1 }, { 1, 0, 2, 1 }, { 2, 0, 3, 1 } }));   // all along X
    return 0;
}
