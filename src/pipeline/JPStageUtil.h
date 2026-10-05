// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "camera/JPImageFile.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

inline namespace jf {

// What several pipeline stages share, as OpenPnP's Utils2D and Java do it.
class JPStageUtil {
public:
    // Java's Math.round: half up, also for negative numbers.
    static long javaRound(double v) { return long(std::floor(v + 0.5)); }
    static bool blank(const std::string& s) { return s.find_first_not_of(" \t") == std::string::npos; }
    // A picture file as OpenCV wants it (BGR, as OpenCV's imread), and back.
    static cv::Mat readPicture(const std::string& path) {
        JPFrame frame;
        std::string error;
        if (!JPImageFile::readPng(path, frame, error)) throw std::runtime_error(error);
        cv::Mat rgba(frame.height, frame.width, CV_8UC4, frame.rgba.data());
        cv::Mat bgr;
        cv::cvtColor(rgba, bgr, cv::COLOR_RGBA2BGR);
        return bgr;
    }
    static void writePicture(const std::string& path, const cv::Mat& mat) {
        cv::Mat rgba;
        if (mat.channels() == 1) cv::cvtColor(mat, rgba, cv::COLOR_GRAY2RGBA);
        else if (mat.channels() == 3) cv::cvtColor(mat, rgba, cv::COLOR_BGR2RGBA);
        else cv::cvtColor(mat, rgba, cv::COLOR_BGRA2RGBA);
        if (rgba.depth() != CV_8U) rgba.convertTo(rgba, CV_8U);
        JPFrame frame;
        frame.width = rgba.cols;
        frame.height = rgba.rows;
        frame.rgba.assign(rgba.data, rgba.data + rgba.total() * 4);
        std::string error;
        if (!JPImageFile::writePng(path, frame, error)) throw std::runtime_error(error);
    }
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
    // A stage's picture as the pipeline editor shows it, RGBA: in true
    // colours from its colour space (OpenCvUtils.toRGB), or as if BGR; 8-bit
    // grey or BGR, or floats 0..1 (OpenCvUtils.toBufferedImage). False and
    // why for one it cannot show.
    static bool toRgba(const cv::Mat& image, const std::string& colorSpace, bool trueColors, cv::Mat& rgba, std::string& why) {
        cv::Mat m = image;
        if (trueColors && !colorSpace.empty()) {
            if (m.channels() != 3 && colorSpace != "Gray") {
                why = "Expecting image to be in the " + colorSpace + " color space but it has only one channel.";
                return false;
            }
            static const std::pair<const char*, int> codes[] = { { "Rgb", cv::COLOR_RGB2BGR },         { "Hls", cv::COLOR_HLS2BGR },
                                                                 { "HlsFull", cv::COLOR_HLS2BGR_FULL }, { "Hsv", cv::COLOR_HSV2BGR },
                                                                 { "HsvFull", cv::COLOR_HSV2BGR_FULL } };
            for (const auto& [name, code] : codes)
                if (colorSpace == name) cv::cvtColor(m, m, code);
        }
        if (m.type() == CV_32F) m.convertTo(m, CV_8UC1, 255);
        else if (m.type() == CV_32FC3) m.convertTo(m, CV_8UC3, 255);
        if (m.type() == CV_8UC1) cv::cvtColor(m, rgba, cv::COLOR_GRAY2RGBA);
        else if (m.type() == CV_8UC3) cv::cvtColor(m, rgba, cv::COLOR_BGR2RGBA);
        else {
            why = "Unsupported Mat: type " + std::to_string(m.type()) + ", channels " + std::to_string(m.channels()) + ", depth "
                  + std::to_string(m.depth());
            return false;
        }
        return true;
    }
    // OpenCvUtils.matMaxima: the local maxima of a one-channel float picture
    // within [rangeMin, rangeMax], row by row, each the top of its 3 x 3.
    static std::vector<cv::Point> matMaxima(const cv::Mat& mat, double rangeMin, double rangeMax) {
        std::vector<cv::Point> out;
        const int rEnd = mat.rows - 1, cEnd = mat.cols - 1;
        auto at = [&mat](int r, int c) { return double(mat.at<float>(r, c)); };
        for (int r = 0; r <= rEnd; ++r) {
            bool before = true;
            double cur = at(r, 0);
            for (int c = 1; c <= cEnd; ++c) {
                const double val = at(r, c);
                if (val == cur) continue;
                if (cur < val) {
                    before = true;
                } else {
                    if (before && rangeMin <= cur && cur <= rangeMax) {
                        if (0 < r && (at(r - 1, c - 1) >= cur || at(r - 1, c) >= cur)) {
                        } else if (r < rEnd && (at(r + 1, c - 1) > cur || at(r + 1, c) > cur)) {
                        } else if (1 < c && ((0 < r && at(r - 1, c - 2) >= cur) || at(r, c - 2) > cur || (r < rEnd && at(r + 1, c - 2) > cur))) {
                        } else {
                            out.emplace_back(c - 1, r);
                        }
                    }
                    if (before) before = false;
                }
                cur = val;
            }
            // The row's end.
            if (before && rangeMin <= cur && cur <= rangeMax && cEnd >= 2) {
                if (0 < r && (at(r - 1, cEnd - 1) >= cur || at(r - 1, cEnd) >= cur)) {
                } else if (r < rEnd && (at(r + 1, cEnd - 1) > cur || at(r + 1, cEnd) > cur)) {
                } else if ((1 < r && at(r - 1, cEnd - 2) >= cur) || at(r, cEnd - 2) > cur || (r < rEnd && at(r + 1, cEnd - 2) > cur)) {
                } else {
                    out.emplace_back(cEnd, r);
                }
            }
        }
        return out;
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
