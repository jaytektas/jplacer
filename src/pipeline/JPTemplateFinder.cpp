// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPTemplateFinder.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>

inline namespace jf {

namespace {

// What a Neoden 4 feeder's area of interest is kept within (OpenPnP's).
constexpr int kNeodenMost = 512;

} // namespace

JPTemplateFinder::Area JPTemplateFinder::Area::placed(int pictureWidth, int pictureHeight) const {
    if (!fromMiddle) return *this;
    auto kept = [](int v) { return std::clamp(v, 0, kNeodenMost); };
    return { kept(x + pictureWidth / 2), kept(y + pictureHeight / 2), kept(width), kept(height), false };
}

JPTemplateFinder::Result JPTemplateFinder::find(const cv::Mat& bgr, const cv::Mat& templ, const Area& area) {
    Result r;
    if (templ.empty()) {
        r.why = "no template image";
        return r;
    }
    if (bgr.empty()) {
        r.why = "no picture";
        return r;
    }
    Area a = area;
    if (a.width <= 0 || a.height <= 0) a = { 0, 0, bgr.cols, bgr.rows };
    const int x0 = std::clamp(a.x, 0, bgr.cols), y0 = std::clamp(a.y, 0, bgr.rows);
    const int x1 = std::clamp(a.x + a.width, 0, bgr.cols), y1 = std::clamp(a.y + a.height, 0, bgr.rows);
    if (x1 - x0 < templ.cols || y1 - y0 < templ.rows) {
        r.why = "the template image is larger than the area of interest";
        return r;
    }
    if (templ.type() != bgr.type()) {
        r.why = "the template image and the picture are not alike (both must be colour)";
        return r;
    }
    const cv::Mat roi = bgr(cv::Rect(x0, y0, x1 - x0, y1 - y0));
    cv::Mat result;
    cv::matchTemplate(roi, templ, result, cv::TM_CCOEFF);
    double least = 0;
    cv::Point lo, hi;
    cv::minMaxLoc(result, &least, &r.score, &lo, &hi);
    r.found = true;
    r.x = hi.x + x0;
    r.y = hi.y + y0;
    return r;
}

} // inline namespace jf
