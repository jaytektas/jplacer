// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <opencv2/core.hpp>

#include <algorithm>
#include <cmath>
#include <string>

inline namespace jf {

// What several pipeline stages share, as OpenPnP's Utils2D and Java do it.
class JPStageUtil {
public:
    // Java's Math.round: half up, also for negative numbers.
    static long javaRound(double v) { return long(std::floor(v + 0.5)); }
    static bool blank(const std::string& s) { return s.find_first_not_of(" \t") == std::string::npos; }
    // HslColor.toRGB: hue in degrees, saturation and luminance in percent, as BGR for OpenCV.
    static cv::Scalar hsl(float h, float s, float l) {
        h = std::fmod(h, 360.0f) / 360.0f;
        s /= 100.0f;
        l /= 100.0f;
        const float q = l < 0.5f ? l * (1 + s) : (l + s) - (s * l);
        const float p = 2 * l - q;
        auto hue = [p, q](float t) {
            if (t < 0) t += 1;
            if (t > 1) t -= 1;
            if (6 * t < 1) return p + (q - p) * 6 * t;
            if (2 * t < 1) return q;
            if (3 * t < 2) return p + (q - p) * 6 * (2.0f / 3.0f - t);
            return p;
        };
        auto byte = [](float v) { return double(int(std::min(1.0f, std::max(0.0f, v)) * 255 + 0.5f)); };
        return cv::Scalar(byte(hue(h - 1.0f / 3.0f)), byte(hue(h)), byte(hue(h + 1.0f / 3.0f)));
    }
    // FluentCv.indexedColor: a different colour for each of a list's items.
    static cv::Scalar indexedColor(int i) {
        const float h = float((i * 59) % 360);
        const float s = float(std::max((i * i) % 100, 80)), l = float(std::max((i * i) % 100, 50));
        return hsl(h, s, l);
    }
    // HslColor.getComplementary: the hue turned half round.
    static cv::Scalar complementary(const cv::Scalar& bgr) {
        const float r = float(bgr[2] / 255), g = float(bgr[1] / 255), b = float(bgr[0] / 255);
        const float lo = std::min(r, std::min(g, b)), hi = std::max(r, std::max(g, b));
        float h = 0;
        if (hi == lo) h = 0;
        else if (hi == r) h = std::fmod(60 * (g - b) / (hi - lo) + 360, 360.0f);
        else if (hi == g) h = 60 * (b - r) / (hi - lo) + 120;
        else h = 60 * (r - g) / (hi - lo) + 240;
        const float l = (hi + lo) / 2;
        const float sat = hi == lo ? 0 : l <= 0.5f ? (hi - lo) / (hi + lo) : (hi - lo) / (2 - hi - lo);
        return hsl(std::fmod(h + 180.0f, 360.0f), sat * 100, l * 100);
    }
    // Utils2D.angleNorm: into ±lim.
    static double angleNorm(double val, double lim = 45.0) {
        const double clip = lim * 2;
        while (std::abs(val) > lim) val += val < 0 ? clip : -clip;
        return val;
    }
    // Utils2D.rotateToExpectedAngle: turned in 90° steps to lie within ±45°
    // of `expectedAngle` (right-handed, unlike the rectangle's own).
    static cv::RotatedRect rotateToExpectedAngle(cv::RotatedRect r, double expectedAngle) {
        const int steps90 = int(javaRound((expectedAngle + r.angle) / 90));
        if (steps90 == 0) return r;
        const float angle = float(angleNorm(r.angle - steps90 * 90, 180));
        if (steps90 & 1) return cv::RotatedRect(r.center, cv::Size2f(r.size.height, r.size.width), angle);
        return cv::RotatedRect(r.center, r.size, angle);
    }
};

} // inline namespace jf
