// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <opencv2/core.hpp>

#include <string>
#include <vector>

inline namespace jf {

// OpenPnP's nozzle tip background calibration: while the tip's runout is
// calibrated, the pictures of it over the camera looking up are kept, the
// tip's middle blotted out (the smallest part it picks), blurred to the
// smallest detail and turned to HSV (hue 0..255 round the circle); from
// all of them, within the largest part's reach, the background's range is
// worked out for bottom vision to mask:
//
//  - Brightness: how dark it is (the brightest and darkest value).
//  - BrightnessAndKeyColor: a dark background with a vivid key colour (a
//    green or blue tip): the darkest value a colour is masked from, and the
//    key colour's hue, saturation and value box that leaves least masked.
//
// What was found is judged in words (the diagnostics), and each picture with
// pixels the mask would not take is kept twice, as it is and with those
// pixels in a signal colour (Show Problems).
class JPBackgroundCalibration {
public:
    enum class Method { None, Brightness, BrightnessAndKeyColor };
    static const char* methodName(Method m);
    static Method      methodFrom(const std::string& name);

    struct Result {
        int minHue = 0, maxHue = 0, minSaturation = 0, maxSaturation = 0, minValue = 0, maxValue = 0;
        std::string          diagnostics;
        std::vector<cv::Mat> problems;   // BGR: each picture with problems, then its problems marked
    };

    explicit JPBackgroundCalibration(Method method) : m_method(method) {}

    // A picture (BGR) of the tip, its middle at `centerX`, `centerY`: blotted
    // out within `blotRadiusPx`, blurred over `kernelPx`, kept as HSV.
    void add(const cv::Mat& bgr, double centerX, double centerY, int blotRadiusPx, int kernelPx);
    size_t pictures() const { return m_images.size(); }
    // From three pictures or more, within `maskRadiusPx` of their middle:
    // false (nothing found) with fewer.
    bool finish(double maskRadiusPx, Result& out) const;

private:
    Method               m_method;
    std::vector<cv::Mat> m_images;   // HSV, full hue range
};

} // inline namespace jf
