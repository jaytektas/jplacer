// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPVisionFeature.h"

#include "pipeline/JPCircularSymmetry.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>
#include <vector>

inline namespace jf {

std::optional<JPVisionFeature::Found> JPVisionFeature::detect(cv::Mat& bgr, int diameterPx, double extraSearch, bool rough,
                                                              bool diagnostics, double& score) {
    score = 0;
    if (bgr.empty() || bgr.depth() != CV_8U || diameterPx < kLeastDiameterPx) return std::nullopt;
    const int side = std::min(bgr.cols, bgr.rows);
    // OpenPnP's getSubjectPixelLocation, the expected feature at the picture's middle.
    const int subjectArea = int(side * kFiducialAreaShare);
    const int maxD = int(diameterPx * kFiducialMargin + 1);
    const int minD = int(diameterPx / kFiducialMargin - 1);
    const int search = int(std::max(double(subjectArea), maxD * kFiducialMargin * 2) + side * extraSearch * 2);
    JPCircularSymmetry::Search q;
    q.xCenter = bgr.cols / 2;
    q.yCenter = bgr.rows / 2;
    q.minDiameter = minD;
    q.maxDiameter = maxD;
    q.searchDiameter = q.searchWidth = q.searchHeight = search;
    q.maxTargetCount = 1;
    q.minSymmetry = kMinSymmetry;
    q.corrSymmetry = 0;
    q.subSampling = kSubSampling;
    q.superSampling = rough ? 1 : kSuperSampling;
    q.diagnostics = diagnostics;
    JPCircularSymmetry::ScoreRange range;
    std::vector<JPPipelineModel::Circle> found;
    try {
        found = JPCircularSymmetry::inPixels(JPCircularSymmetry::find(bgr, q, range));
    } catch (const std::exception&) {
        return std::nullopt;
    }
    if (std::isfinite(range.finalScore)) score = range.finalScore;
    if (found.empty()) return std::nullopt;
    return Found { found.front().x, found.front().y, found.front().diameter, score };
}

std::optional<int> JPVisionFeature::next(const cv::Mat& bgr, int fromPx) {
    const int maxD = maxDiameter(bgr.cols, bgr.rows);
    // Every diameter tried, its score kept (none found: 0).
    std::vector<int> diameters;
    std::vector<double> scores;
    for (double d = kFirstTriedPx; d <= maxD; d = d * std::pow(kFiducialMargin, 0.2) + 1) {
        cv::Mat picture = bgr.clone();
        double score = 0;
        detect(picture, int(d), kPreviewSearch, true, false, score);
        diameters.push_back(int(std::lround(d)));
        scores.push_back(score);
    }
    // The next diameter bigger than `fromPx` whose score is the best of the kernel's around it (the kernel's middle
    // after the first); not found: round again from the smallest.
    std::optional<int> best;
    for (int wrap = 0; wrap < 2 && !best; ++wrap)
        for (size_t i = 0; i + kKernel <= diameters.size(); ++i) {
            if (wrap == 0 && diameters[i] <= fromPx) continue;
            double bestScore = 0;
            int at = -1;
            for (int k = 0; k < kKernel; ++k)
                if (bestScore < scores[i + k]) {
                    bestScore = scores[i + k];
                    at = int(i) + k;
                }
            if (at == int(i) + kKernel / 2 + 1) {
                best = diameters[size_t(at)];
                break;
            }
        }
    if (!best) return std::nullopt;
    // Measured again at it: the diameter it has.
    cv::Mat picture = bgr.clone();
    double score = 0;
    const auto found = detect(picture, *best, kPreviewSearch, true, false, score);
    if (!found) return best;
    return int(std::lround(found->diameter));
}

} // inline namespace jf
