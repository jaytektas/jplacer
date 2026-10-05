// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// OpenPnP's nozzle tip background calibration: from pictures of a tip over
// a background, Brightness gives the background's darkest and brightest;
// BrightnessAndKeyColor finds a vivid key colour's hue box on a dark
// background; a bright patch is reported as a problem.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "tasks/JPBackgroundCalibration.h"

#include <opencv2/imgproc.hpp>

using namespace jf;

namespace {

// A dark grey picture (value ~40) with a green ring round its middle, and the tip (white) in the middle.
cv::Mat picture(bool brightPatch) {
    cv::Mat img(120, 160, CV_8UC3, cv::Scalar(40, 40, 40));
    cv::circle(img, cv::Point(80, 60), 30, cv::Scalar(30, 200, 30), 12);
    cv::circle(img, cv::Point(80, 60), 8, cv::Scalar(255, 255, 255), cv::FILLED);
    if (brightPatch) cv::rectangle(img, cv::Rect(100, 20, 10, 10), cv::Scalar(230, 230, 230), cv::FILLED);
    return img;
}

} // namespace

int main() {
    {
        JPBackgroundCalibration b(JPBackgroundCalibration::Method::Brightness);
        for (int i = 0; i < 2; ++i) b.add(picture(false), 80, 60, 10, 3);
        JPBackgroundCalibration::Result r;
        assert(!b.finish(55, r));   // too few pictures
        b.add(picture(false), 80, 60, 10, 3);
        assert(b.finish(55, r));
        // The tip blotted out: the brightest is the green ring, the darkest the grey.
        assert(r.maxValue >= 190 && r.maxValue <= 205 && r.minValue <= 40);
        assert(r.minHue == 0 && r.maxHue == 255);
        assert(r.diagnostics.find("too bright") != std::string::npos && !r.problems.empty());
    }
    {
        JPBackgroundCalibration b(JPBackgroundCalibration::Method::BrightnessAndKeyColor);
        for (int i = 0; i < 3; ++i) b.add(picture(false), 80, 60, 10, 3);
        JPBackgroundCalibration::Result r;
        assert(b.finish(55, r));
        // Green: hue about 85 of 255, inside the box found; the grey masked by brightness.
        assert(r.minHue <= 85 && r.maxHue >= 85 && ((r.maxHue - r.minHue) & 0xFF) <= 255 / 6);
        assert(r.minSaturation >= 255 / 4 && r.minValue >= 32 && r.minValue <= 128);
        assert(r.problems.empty());
        assert(r.diagnostics.find("The key color is") != std::string::npos);
    }
    {
        // A bright, grey spot in the background: a problem.
        JPBackgroundCalibration b(JPBackgroundCalibration::Method::BrightnessAndKeyColor);
        for (int i = 0; i < 3; ++i) b.add(picture(true), 80, 60, 10, 3);
        JPBackgroundCalibration::Result r;
        assert(b.finish(55, r) && r.problems.size() == 6);
    }
    return 0;
}
