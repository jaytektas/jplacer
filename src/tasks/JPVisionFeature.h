// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <opencv2/core.hpp>

#include <optional>

inline namespace jf {

// OpenPnP's VisionSolutions feature detection (getSubjectPixelLocation, one shot): a round feature of about a given
// diameter (pixels) near the middle of a picture, by circular symmetry (JPCircularSymmetry) between the diameter
// shrunk and grown by its fiducial margin, searched over a fifth of the picture (or more, for a big one); and its
// Auto-Detect Next: every diameter from 10 pixels up tried, and the next one bigger than the one now that scores best
// among its neighbours taken (round again to the smallest when there is none), then measured.
class JPVisionFeature {
public:
    struct Found {
        double x = 0, y = 0, diameter = 0;   // pixels
        double score = 0;                    // how circular (its final score)
    };

    // The feature of about `diameterPx` in `bgr` (8 bits a channel), searched `extraSearch` of the picture's smaller
    // side further out (OpenPnP's previews: kPreviewSearch); `diagnostics` draws the search on it (the circle found
    // and its cross-hairs, or the nominal circle dashed). `rough`: no super-sampling (a preview). The score is set
    // even when nothing is found.
    static std::optional<Found> detect(cv::Mat& bgr, int diameterPx, double extraSearch, bool rough, bool diagnostics, double& score);
    // The same, expected at (`x`, `y`) in the picture rather than its middle (OpenPnP's expected offsets).
    static std::optional<Found> detectAt(cv::Mat& bgr, double x, double y, int diameterPx, double extraSearch, bool rough,
                                         bool diagnostics, double& score);
    // The biggest feature a picture can show (OpenPnP's maxCameraRelativeSubjectDiameter of its smaller side).
    static int maxDiameter(int width, int height) { return int((width < height ? width : height) * kSubjectShare); }
    // Auto-Detect Next from `fromPx`: the diameter (as measured) of the next feature; none when there is none.
    static std::optional<int> next(const cv::Mat& bgr, int fromPx);

    // OpenPnP's VisionSolutions defaults.
    static constexpr double kMinSymmetry = 1.5;
    static constexpr int    kSubSampling = 8, kSuperSampling = 4;
    static constexpr double kFiducialMargin = 1.1;
    static constexpr double kFiducialAreaShare = 0.2, kSubjectShare = 0.7;
    static constexpr int    kLeastDiameterPx = 3, kFirstTriedPx = 10, kKernel = 9;
    static constexpr double kPreviewSearch = 0.05;
};

} // inline namespace jf
