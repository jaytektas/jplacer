// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPPipelineModel.h"

#include <opencv2/core.hpp>

#include <cmath>
#include <vector>

inline namespace jf {

// OpenPnP's DetectCircularSymmetry.findCircularSymmetry: where in a search
// area round a nominal centre the picture is most nearly the same all the
// way round (the variance across the rings against the variance within
// them), sampled coarsely first, then searched finer round the best; the
// circle's diameter is the ring of the most contrast. With diagnostics or a
// heat map, the picture is drawn on.
class JPCircularSymmetry {
public:
    enum class Score { OverallVarianceVsRingVarianceSum, RingAvgeragesVarianceVsRingVarianceSum, RingMedianVarianceVsRingVarianceSum };
    // The scores seen (for the heat map); `finalScore` the last pass's best.
    struct ScoreRange {
        double minScore = INFINITY, maxScore = -INFINITY, finalScore = -INFINITY;
        void   add(double score);
        double heat(double score) const;

    private:
        double m_sum = 0;
        int    m_n = 0;
    };
    struct Search {
        int    xCenter = 0, yCenter = 0;
        int    minDiameter = 10, maxDiameter = 100;
        int    searchDiameter = 200, searchWidth = 200, searchHeight = 200;
        int    maxTargetCount = 1;
        double minSymmetry = 1.2, corrSymmetry = 0;
        int    subSampling = 8, superSampling = 1;
        Score  score = Score::OverallVarianceVsRingVarianceSum;
        bool   diagnostics = false, heatMap = false;
    };

    // The circles found, best first (with their scores). The picture must be 8 bits a channel.
    static std::vector<JPPipelineModel::Circle> find(cv::Mat& image, Search search, ScoreRange& range);
};

} // inline namespace jf
