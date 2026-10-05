// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <opencv2/core.hpp>

#include <cmath>
#include <optional>

inline namespace jf {

// OpenPnP's DetectRectlinearSymmetry.findReclinearSymmetry: the angle at
// which the picture's horizontal and vertical cross-sections have the most
// contrast, then the middle of each cross-section about which it is most
// nearly mirrored, and the subject's bounds; searched coarsely first, then
// finer round the best. With diagnostics, the picture is drawn on.
class JPRectlinearSymmetry {
public:
    // How a cross-section is judged mirrored (OpenPnP's SymmetryFunction).
    enum class Function { FullSymmetry, EdgeSymmetry, OutlineSymmetry, OutlineEdgeSymmetry, OutlineSymmetryMasked };
    struct ScoreRange {
        double minScore = INFINITY, maxScore = -INFINITY, finalScore = -INFINITY;
        void   add(double score);
    };
    struct Search {
        int      xCenter = 0, yCenter = 0;
        double   expectedAngle = 0;   // degrees
        double   maxWidth = 100, maxHeight = 100, searchDistance = 100, searchAngle = 45;
        double   minSymmetry = 10;
        Function xFunction = Function::FullSymmetry, yFunction = Function::FullSymmetry;
        double   minFeatureSize = 40;
        int      subSampling = 8, superSampling = 1, smoothing = 5;
        double   gamma = 2.5;
        int      threshold = 128;
        bool     diagnostics = false, diagnosticsMap = false;
    };

    // The subject, or none when not symmetric enough. The picture must be 8 bits a channel.
    static std::optional<cv::RotatedRect> find(cv::Mat& image, Search search, ScoreRange& range);
};

} // inline namespace jf
