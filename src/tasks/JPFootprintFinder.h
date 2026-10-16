// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <opencv2/core.hpp>

#include <vector>

inline namespace jf {

// A footprint's pads found in a picture, as OpenPnP's template fiducial
// locator finds a footprint (CreateFootprintTemplateImage, MatchTemplate):
// its pads drawn bright on dark where they should be, then matched against
// the picture by normalised correlation within `searchPx` of there, the best
// place refined to a fraction of a pixel. Only the pads' own pattern counts:
// a neighbour or silkscreen nearby matches it worse than the footprint does.
class JPFootprintFinder {
public:
    struct Result {
        bool   found = false;
        double dx = 0, dy = 0;   // how far the pads are from where they were drawn (pixels)
        double score = 0;        // the correlation there (1: the same picture)
    };
    // `pads`: each pad's outline in the picture's pixels, where it should be. A match scoring under `minScore`
    // is not taken.
    static Result find(const cv::Mat& bgr, const std::vector<std::vector<cv::Point2d>>& pads, double searchPx, double minScore);
    // Dark round the pads in the template, this share of its smaller side (at least kLeastMarginPx).
    static constexpr double kMarginShare = 0.3;
    static constexpr int    kLeastMarginPx = 4;
};

} // inline namespace jf
