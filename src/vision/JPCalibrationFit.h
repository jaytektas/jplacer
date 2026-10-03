// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <array>
#include <optional>
#include <vector>

inline namespace jf {

// The least-squares fit behind a camera calibration: where a mark appeared in
// the picture (pixels) for each known head offset (mm), as
//   pixel = centre + M * offset
// through a perfect lens, and with fitWithLens through one that bends the
// picture radially (JPLens): the pixel is where that point is then seen.
class JPCalibrationFit {
public:
    struct Sample {
        double dxMm = 0, dyMm = 0;   // head offset from the start
        double xPx = 0, yPx = 0;     // where the mark was seen
    };
    struct Result {
        std::array<double, 4> pxPerMm{};   // M, row-major
        double centreX = 0, centreY = 0;   // where the mark was with no offset
        double rmsPx = 0;                  // residual
        double lensK1 = 0, lensK2 = 0;     // JPLens::k1, k2 (zero from fit)
        double lensCentreX = 0, lensCentreY = 0;   // JPLens's centre (fitWithLens)
    };

    // Nothing when the samples do not span two directions (at least three,
    // not in a line).
    static std::optional<Result> fit(const std::vector<Sample>& samples);
    // The same with the lens fitted too, for a picture width x height: its
    // bending (k1), and with `lensCentre` also where it bends about (else the
    // picture's middle) and how the bending grows to the corners (k2). Needs
    // more samples than parameters (seven, or ten), spread across the picture
    // so the bending shows.
    static std::optional<Result> fitWithLens(const std::vector<Sample>& samples, int width, int height,
                                             bool lensCentre);
    // How far each sample sits from where a fit with the lens puts it (pixels).
    static std::vector<double> residualsPx(const std::vector<Sample>& samples, const Result& fit, int width, int height);
    // The same along x and y: where each sample was seen less where the fit puts it.
    static std::vector<std::array<double, 2>> residualVectorsPx(const std::vector<Sample>& samples, const Result& fit,
                                                                int width, int height);
};

} // inline namespace jf
