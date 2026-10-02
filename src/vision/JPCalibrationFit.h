// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <array>
#include <optional>
#include <vector>

inline namespace jf {

// The least-squares fit behind a camera calibration: where a mark appeared in
// the picture (pixels) for each known head offset (mm), as
//   pixel = centre + M * offset.
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
    };

    // Nothing when the samples do not span two directions (at least three,
    // not in a line).
    static std::optional<Result> fit(const std::vector<Sample>& samples);
};

} // inline namespace jf
