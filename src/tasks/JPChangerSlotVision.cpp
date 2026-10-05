// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPChangerSlotVision.h"

#include <opencv2/imgproc.hpp>

#include <cmath>
#include <string>

inline namespace jf {

int JPChangerSlotVision::evenPixels(double mm, double mmPerPixel) {
    return mmPerPixel > 0 ? int(std::ceil(mm / mmPerPixel)) & ~1 : 0;
}

cv::Rect JPChangerSlotVision::middle(const cv::Mat& picture, int width, int height) {
    return cv::Rect((picture.cols - width) / 2, (picture.rows - height) / 2, width, height) & cv::Rect(0, 0, picture.cols, picture.rows);
}

bool JPChangerSlotVision::find(const cv::Mat& picture, const cv::Mat& templ, int width, int height, Match& match, std::string& why) {
    if (picture.empty() || templ.empty()) {
        why = "no picture to look in";
        return false;
    }
    const cv::Rect area = middle(picture, width, height);
    if (area.width < templ.cols || area.height < templ.rows) {
        why = "the template (" + std::to_string(templ.cols) + " x " + std::to_string(templ.rows)
            + " pixels) does not fit in the picture searched (" + std::to_string(area.width) + " x " + std::to_string(area.height) + ")";
        return false;
    }
    // The camera's picture made the template's kind, as matchTemplate needs.
    cv::Mat searched = picture(area);
    if (searched.channels() != templ.channels()) {
        cv::Mat converted;
        if (templ.channels() == 1) cv::cvtColor(searched, converted, searched.channels() == 4 ? cv::COLOR_BGRA2GRAY : cv::COLOR_BGR2GRAY);
        else if (searched.channels() == 1) cv::cvtColor(searched, converted, templ.channels() == 4 ? cv::COLOR_GRAY2BGRA : cv::COLOR_GRAY2BGR);
        else cv::cvtColor(searched, converted, templ.channels() == 4 ? cv::COLOR_BGR2BGRA : cv::COLOR_BGRA2BGR);
        searched = converted;
    }
    cv::Mat result;
    cv::matchTemplate(searched, templ, result, cv::TM_CCOEFF_NORMED);
    double best = 0;
    cv::Point at;
    cv::minMaxLoc(result, nullptr, &best, nullptr, &at);
    match.score = best;
    // Centred, the template's corner is at (cols - 1) / 2 of the result.
    match.dxPx = at.x - (result.cols - 1) / 2.0;
    match.dyPx = at.y - (result.rows - 1) / 2.0;
    return true;
}

} // inline namespace jf
