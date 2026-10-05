// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// OpenPnP's auto focus score: a sharp picture scores higher than the same
// blurred, and both higher than a flat one (no edges: at most 1, the fractile's
// share of the zero edges); with the marked
// crop, the circle's crop with its hardest edges in green.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "tasks/JPAutoFocus.h"

#include <opencv2/imgproc.hpp>

using namespace jf;

namespace {

JPFrame frameOf(const cv::Mat& bgr) {
    cv::Mat rgba;
    cv::cvtColor(bgr, rgba, cv::COLOR_BGR2RGBA);
    JPFrame f;
    f.width = rgba.cols;
    f.height = rgba.rows;
    f.rgba.assign(rgba.data, rgba.data + rgba.total() * 4);
    return f;
}

} // namespace

int main() {
    cv::Mat sharp(240, 320, CV_8UC3, cv::Scalar(30, 30, 30));
    for (int i = 0; i < 8; ++i) cv::rectangle(sharp, cv::Rect(100 + i * 15, 80, 8, 80), cv::Scalar(220, 220, 220), cv::FILLED);
    cv::Mat blurred;
    cv::GaussianBlur(sharp, blurred, cv::Size(15, 15), 4);
    const cv::Mat flat(240, 320, CV_8UC3, cv::Scalar(90, 90, 90));
    const double s = JPAutoFocus::focusScore(frameOf(sharp), 150);
    const double b = JPAutoFocus::focusScore(frameOf(blurred), 150);
    const double f = JPAutoFocus::focusScore(frameOf(flat), 150);
    assert(s > b && b > f && f <= 1);
    JPFrame marked;
    JPAutoFocus::focusScore(frameOf(sharp), 150, &marked);
    assert(marked.width == 151 && marked.height == 151);
    bool green = false;
    for (size_t i = 0; i + 3 < marked.rgba.size(); i += 4)
        green = green || (marked.rgba[i] == 0 && marked.rgba[i + 1] == 255 && marked.rgba[i + 2] == 0);
    assert(green);
    return 0;
}
