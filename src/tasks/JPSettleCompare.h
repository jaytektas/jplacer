// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "camera/JPFrame.h"
#include "machine/JPCameraConfig.h"

#include <opencv2/core.hpp>

inline namespace jf {

// OpenPnP's camera settling comparison (AbstractSettlingCamera): each picture
// prepared as the camera's Settle says (grey unless full colour, cut to the
// centre mask, contrast enhanced, scaled down for a large blur, blurred, its
// gradients taken), then compared with the one before by the method: the
// norm of the difference as a percentage of full scale (Maximum, Mean,
// Euclidean, Square), or how far in pixels it moved (Motion). One for each settle.
class JPSettleCompare {
public:
    // `method`: the comparison (FixedTime's graph compares as Euclidean).
    JPSettleCompare(const JPCameraConfig::Settle& settle, std::string method);
    cv::Mat prepare(const JPFrame& frame);
    double  difference(const cv::Mat& before, const cv::Mat& now) const;

    // OpenPnP's: a motion larger than this share of the picture is taken as
    // no match; a match scoring under this is none; contrast is stretched
    // over at least this range of levels.
    static constexpr double kMaxRelativeMotion = 0.05;
    static constexpr double kMinMotionScore = 0.9;
    static constexpr double kMinContrastRange = 16;
    // A blur wider than this is done on the picture scaled down.
    static constexpr int kLargestBlurKernel = 5;

private:
    cv::Mat enhanceContrast(const cv::Mat& mat, const cv::Mat& mask) const;
    static cv::Mat circleMask(const cv::Mat& mat, int diameter);

    JPCameraConfig::Settle m_settle;
    std::string            m_method;
    cv::Mat                m_mask, m_maskFullSize;
};

} // inline namespace jf
