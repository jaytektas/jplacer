// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPFootprintFinder.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>

inline namespace jf {

namespace {

// The peak of a parabola through three samples, from the middle one (-0.5..0.5).
double vertex(double before, double at, double after) {
    const double d = before - 2 * at + after;
    if (d >= 0) return 0;   // not a peak
    return std::clamp(0.5 * (before - after) / d, -0.5, 0.5);
}

} // namespace

JPFootprintFinder::Result JPFootprintFinder::find(const cv::Mat& bgr, const std::vector<std::vector<cv::Point2d>>& pads,
                                                  double searchPx, double minScore) {
    Result r;
    if (bgr.empty() || pads.empty()) return r;
    double x0 = INFINITY, y0 = INFINITY, x1 = -INFINITY, y1 = -INFINITY;
    for (const auto& pad : pads)
        for (const cv::Point2d& p : pad) {
            x0 = std::min(x0, p.x);
            y0 = std::min(y0, p.y);
            x1 = std::max(x1, p.x);
            y1 = std::max(y1, p.y);
        }
    if (!(x1 > x0) || !(y1 > y0)) return r;
    const int margin = std::max(kLeastMarginPx, int(std::lround(kMarginShare * std::min(x1 - x0, y1 - y0))));
    // The template: whole pixels round the pads, the pads drawn where they fall in it.
    const int tx = int(std::floor(x0)) - margin, ty = int(std::floor(y0)) - margin;
    const int tw = int(std::ceil(x1)) + margin - tx, th = int(std::ceil(y1)) + margin - ty;
    cv::Mat templ = cv::Mat::zeros(th, tw, CV_8UC1);
    constexpr int kShift = 4;   // sub-pixel corners for fillPoly
    std::vector<std::vector<cv::Point>> polys;
    for (const auto& pad : pads) {
        std::vector<cv::Point> poly;
        for (const cv::Point2d& p : pad)
            poly.emplace_back(int(std::lround((p.x - tx) * (1 << kShift))), int(std::lround((p.y - ty) * (1 << kShift))));
        polys.push_back(std::move(poly));
    }
    cv::fillPoly(templ, polys, cv::Scalar(255), cv::LINE_AA, kShift);
    // Where to look: the template's place, wider by the search each way, within the picture.
    const int s = int(std::ceil(searchPx));
    const cv::Rect want(tx - s, ty - s, tw + 2 * s, th + 2 * s);
    const cv::Rect roi = want & cv::Rect(0, 0, bgr.cols, bgr.rows);
    if (roi.width < tw + 2 || roi.height < th + 2) return r;
    cv::Mat grey;
    if (bgr.channels() == 3) cv::cvtColor(bgr(roi), grey, cv::COLOR_BGR2GRAY);
    else grey = bgr(roi).clone();
    cv::Mat scores;
    cv::matchTemplate(grey, templ, scores, cv::TM_CCOEFF_NORMED);
    double best = 0;
    cv::Point at;
    cv::minMaxLoc(scores, nullptr, &best, nullptr, &at);
    r.score = best;
    if (best < minScore) return r;
    double fx = at.x, fy = at.y;
    if (at.x > 0 && at.x + 1 < scores.cols)
        fx += vertex(scores.at<float>(at.y, at.x - 1), scores.at<float>(at.y, at.x), scores.at<float>(at.y, at.x + 1));
    if (at.y > 0 && at.y + 1 < scores.rows)
        fy += vertex(scores.at<float>(at.y - 1, at.x), scores.at<float>(at.y, at.x), scores.at<float>(at.y + 1, at.x));
    // Where the template's corner landed against where it was drawn.
    r.dx = roi.x + fx - tx;
    r.dy = roi.y + fy - ty;
    // Only within the search: a best at the window's edge is something else.
    if (std::hypot(r.dx, r.dy) > searchPx + 1) return r;
    r.found = true;
    return r;
}

} // inline namespace jf
