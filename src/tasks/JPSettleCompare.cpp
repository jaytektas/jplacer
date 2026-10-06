// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPSettleCompare.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>

inline namespace jf {

JPSettleCompare::JPSettleCompare(const JPCameraConfig::Settle& settle, std::string method)
    : m_settle(settle), m_method(std::move(method)) {
    // Odd, as OpenPnP keeps it; 1 or less: none.
    m_settle.gaussianBlur = m_settle.gaussianBlur <= 1 ? 0 : (m_settle.gaussianBlur | 1);
}

cv::Mat JPSettleCompare::circleMask(const cv::Mat& mat, int diameter) {
    cv::Mat mask(mat.rows, mat.cols, CV_8U, cv::Scalar::all(0));
    cv::circle(mask, cv::Point(mat.cols / 2, mat.rows / 2), diameter / 2, cv::Scalar::all(255), cv::FILLED);
    return mask;
}

cv::Mat JPSettleCompare::prepare(const JPFrame& frame) {
    cv::Mat rgba(frame.height, frame.width, CV_8UC4, const_cast<uint8_t*>(frame.rgba.data()));
    cv::Mat mat;
    cv::cvtColor(rgba, mat, m_settle.fullColor ? cv::COLOR_RGBA2BGR : cv::COLOR_RGBA2GRAY);
    // A large blur is done on the picture scaled down: a box blur, then a Gaussian one.
    int blur = m_settle.gaussianBlur;
    const int divisor = blur > kLargestBlurKernel ? (blur + kLargestBlurKernel / 2) / kLargestBlurKernel : 1;
    int maskDiameter = 0;
    if (m_settle.maskCircle > 0) {
        // Cut to the mask, in multiples of twice the divisor.
        const int dimension = std::min(mat.rows, mat.cols);
        maskDiameter = std::max(1, int(m_settle.maskCircle * dimension));
        int width = std::min(mat.cols, maskDiameter), height = std::min(mat.rows, maskDiameter);
        maskDiameter = maskDiameter / divisor / 2 * divisor * 2;
        width = width / divisor / 2 * divisor * 2;
        height = height / divisor / 2 * divisor * 2;
        if (width > 0 && height > 0) mat = mat(cv::Rect((mat.cols - width) / 2, (mat.rows - height) / 2, width, height)).clone();
        if (m_maskFullSize.empty()) {
            m_maskFullSize = circleMask(mat, maskDiameter);
            if (divisor == 1) m_mask = m_maskFullSize;
        }
    }
    // Before scaling down, so mixed colours come from the full range.
    if (m_settle.contrastEnhance > 0) mat = enhanceContrast(mat, m_maskFullSize);
    if (divisor > 1) {
        blur = (m_settle.gaussianBlur / divisor) | 1;
        cv::Mat smaller;
        cv::resize(mat, smaller, cv::Size(mat.cols / divisor, mat.rows / divisor), 0, 0, cv::INTER_AREA);
        mat = smaller;
        maskDiameter /= divisor;
    }
    if (maskDiameter > 0 && m_mask.empty()) m_mask = circleMask(mat, maskDiameter);
    if (blur > 1) cv::GaussianBlur(mat, mat, cv::Size(blur | 1, blur | 1), 0);
    if (m_settle.gradients) {
        cv::Mat gradient;
        cv::Laplacian(mat, gradient, CV_16S, 3, 1, 0, cv::BORDER_REPLICATE);
        cv::convertScaleAbs(gradient, mat);
    }
    return mat;
}

cv::Mat JPSettleCompare::enhanceContrast(const cv::Mat& mat, const cv::Mat& mask) const {
    // The levels stretched from the darkest to the brightest, over at least kMinContrastRange.
    double most = kMinContrastRange, range = kMinContrastRange;
    std::vector<cv::Mat> channels;
    cv::split(mat, channels);
    for (const cv::Mat& c : channels) {
        double lo = 0, hi = 0;
        cv::minMaxLoc(c, &lo, &hi, nullptr, nullptr, mask.empty() ? cv::noArray() : cv::InputArray(mask));
        most = std::max(most, hi);
        range = std::max(range, hi - lo);
    }
    most /= 255.0;
    range /= 255.0;
    const double e = m_settle.contrastEnhance;
    const double scale = e / range + (1.0 - e), offset = -(most - range) * e / range;
    cv::Mat out;
    cv::convertScaleAbs(mat, out, scale, offset * 255.0);
    return out;
}

double JPSettleCompare::difference(const cv::Mat& before, const cv::Mat& now) const {
    if (before.empty() || before.size() != now.size() || before.type() != now.type()) return 100;
    if (m_method == "Motion") {
        // How far the picture moved: its middle looked for in the one before.
        const int dimension = std::min(now.cols, now.rows);
        const int margin = int(std::lround(dimension * kMaxRelativeMotion));
        const int w = now.cols - 2 * margin, h = now.rows - 2 * margin;
        if (w <= 0 || h <= 0) return 0;
        const cv::Mat templ = now(cv::Rect(margin, margin, w, h));
        cv::Mat result;
        if (!m_mask.empty()) cv::matchTemplate(before, templ, result, cv::TM_CCOEFF_NORMED, circleMask(templ, std::max(w, h)));
        else cv::matchTemplate(before, templ, result, cv::TM_CCOEFF_NORMED);
        double best = 0;
        cv::Point at;
        cv::minMaxLoc(result, nullptr, &best, nullptr, &at);
        if (best > kMinMotionScore)
            // The distance, and the score as a kind of sub-pixel part.
            return std::hypot(at.x - margin, at.y - margin) + std::pow((1 - best) / (1 - kMinMotionScore), 0.25);
        // Not found: as far as it can tell, and the score as the fraction.
        return std::hypot(margin, margin) + (1 - best);
    }
    int norm = cv::NORM_L2;
    const double pixels = double(now.cols) * now.rows * now.channels();
    double scale = 255.0 * std::sqrt(pixels);
    if (m_method == "Maximum") {
        norm = cv::NORM_INF;
        scale = 255.0;
    } else if (m_method == "Mean") {
        norm = cv::NORM_L1;
        scale = 255.0 * pixels;
    } else if (m_method == "Square") {
        norm = cv::NORM_L2SQR;
        scale = 255.0 * 255.0 * pixels;
    }
    const cv::InputArray mask = m_mask.empty() ? cv::noArray() : cv::InputArray(m_mask);
    double result = cv::norm(before, now, norm, mask) / scale, range = 1.0;
    if (m_settle.contrastEnhance != 0.0) range = cv::norm(now, norm, mask) / scale;
    if (range != 0.0) result *= m_settle.contrastEnhance / range + (1.0 - m_settle.contrastEnhance);
    return result * 100.0;
}

} // inline namespace jf
