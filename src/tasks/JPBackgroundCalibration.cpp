// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPBackgroundCalibration.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

inline namespace jf {

namespace {

// OpenPnP's limits: brightness (value, 0..255) at which hue masking is
// considered; the least saturation of a key colour, its widest hue span
// (60°), and the brightest a background (less its key colour) should be.
constexpr int kMinMaskValue = 32, kMaxMaskValue = 128;
constexpr int kWorstSaturation = 255 / 4, kWorstHueSpan = 255 / 6, kWorstValue = 255 / 2;

// An HSV pixel (full hue range) as BGR.
cv::Vec3b bgrOf(int h, int s, int v) {
    cv::Mat hsv(1, 1, CV_8UC3, cv::Scalar(h, s, v)), bgr;
    cv::cvtColor(hsv, bgr, cv::COLOR_HSV2BGR_FULL);
    return bgr.at<cv::Vec3b>(0, 0);
}

} // namespace

const char* JPBackgroundCalibration::methodName(Method m) {
    switch (m) {
        case Method::None: return "None";
        case Method::Brightness: return "Brightness";
        case Method::BrightnessAndKeyColor: return "BrightnessAndKeyColor";
    }
    return "None";
}

JPBackgroundCalibration::Method JPBackgroundCalibration::methodFrom(const std::string& name) {
    if (name == "Brightness") return Method::Brightness;
    if (name == "BrightnessAndKeyColor") return Method::BrightnessAndKeyColor;
    return Method::None;
}

void JPBackgroundCalibration::add(const cv::Mat& bgr, double centerX, double centerY, int blotRadiusPx, int kernelPx) {
    if (m_method == Method::None || bgr.empty()) return;
    cv::Mat image = bgr.clone();
    // The tip's middle blotted out.
    cv::circle(image, cv::Point(int(std::lround(centerX)), int(std::lround(centerY))), blotRadiusPx, cv::Scalar(0, 0, 0), cv::FILLED,
               cv::LINE_8);
    const int k = std::max(1, kernelPx) | 1;
    cv::GaussianBlur(image, image, cv::Size(k, k), 0);
    cv::Mat hsv;
    cv::cvtColor(image, hsv, cv::COLOR_BGR2HSV_FULL);
    m_images.push_back(hsv);
}

bool JPBackgroundCalibration::finish(double maskRadiusPx, Result& out) const {
    if (m_method == Method::None || m_images.size() < 3) return false;
    out = Result();
    const int maskSq = int(std::pow(maskRadiusPx, 2));
    const int rows = m_images.front().rows, cols = m_images.front().cols;
    // Each pixel within the mask: (hue, saturation, value).
    auto each = [&](const cv::Mat& img, auto&& fn) {
        for (int y = 0; y < rows; ++y)
            for (int x = 0; x < cols; ++x) {
                const int dx = x - cols / 2, dy = y - rows / 2;
                if (dx * dx + dy * dy >= maskSq) continue;
                const cv::Vec3b& p = img.at<cv::Vec3b>(y, x);
                fn(x, y, int(p[0]), int(p[1]), int(p[2]));
            }
    };
    // The pictures with problems: as they are, and with the problem pixels in `signal`.
    auto problemsOf = [&](auto&& outside, const cv::Vec3b& signal) {
        for (const cv::Mat& img : m_images) {
            if (img.rows != rows || img.cols != cols) continue;
            cv::Mat all, seen(rows, cols, CV_8UC3, cv::Scalar(0, 0, 0)), marked(rows, cols, CV_8UC3, cv::Scalar(0, 0, 0));
            cv::cvtColor(img, all, cv::COLOR_HSV2BGR_FULL);
            bool any = false;
            each(img, [&](int x, int y, int h, int s, int v) {
                const cv::Vec3b c = all.at<cv::Vec3b>(y, x);
                seen.at<cv::Vec3b>(y, x) = c;
                const bool bad = outside(h, s, v);
                marked.at<cv::Vec3b>(y, x) = bad ? signal : c;
                any = any || bad;
            });
            if (any) {
                out.problems.push_back(seen);
                out.problems.push_back(marked);
            }
        }
    };
    if (m_method == Method::BrightnessAndKeyColor) {
        std::vector<int> hueValue(256 * 256, 0);
        std::array<int, 256> mono {}, minSat {}, maxSat {};
        minSat.fill(255);
        int maxValue = 0;
        for (const cv::Mat& img : m_images) {
            if (img.rows != rows || img.cols != cols) continue;
            each(img, [&](int, int, int h, int s, int v) {
                if (s >= kWorstSaturation) {
                    hueValue[size_t(h * 256 + v)]++;
                    minSat[size_t(v)] = std::min(minSat[size_t(v)], s);
                    maxSat[size_t(v)] = std::max(maxSat[size_t(v)], s);
                } else {
                    mono[size_t(v)]++;
                }
            });
        }
        // Summed at each value from those at it and above.
        for (int h = 0; h < 256; ++h)
            for (int v = 1; v < 256; ++v)
                if (const int count = hueValue[size_t(h * 256 + v)]; count > 0) {
                    for (int vs = 0; vs < v; ++vs) {
                        hueValue[size_t(h * 256 + vs)] += count;
                        minSat[size_t(vs)] = std::min(minSat[size_t(vs)], minSat[size_t(v)]);
                        maxSat[size_t(vs)] = std::max(maxSat[size_t(vs)], maxSat[size_t(v)]);
                    }
                    maxValue = std::max(maxValue, v);
                }
        for (int v = 1; v < 256; ++v)
            for (int vs = 0; vs < v; ++vs) {
                minSat[size_t(vs)] = std::min(minSat[size_t(vs)], minSat[size_t(v)]);
                maxSat[size_t(vs)] = std::max(maxSat[size_t(vs)], maxSat[size_t(v)]);
                mono[size_t(vs)] += mono[size_t(v)];
            }
        // The best monochrome cut-off and key colour box: least masked.
        long bestMasked = std::numeric_limits<long>::max();
        int bestMinHue = 0, bestMaxHue = 0, bestMinSat = 0, bestMaxSat = 0, bestMaxValue = 0, bestMinValue = 0;
        for (int minValue = kMinMaskValue; minValue <= kMaxMaskValue; ++minValue) {
            if (mono[size_t(minValue)] != 0 && minValue != kMaxMaskValue) continue;
            // The largest gap in hue (round the circle).
            int largestGap = 0, minHue = 0, maxHue = 0;
            for (int h = 0; h < 256; ++h) {
                int gap = 0;
                while (hueValue[size_t(((h + gap) & 0xFF) * 256 + minValue)] == 0 && gap < 255) ++gap;
                if (gap > largestGap) {
                    largestGap = gap;
                    maxHue = (h - 1) & 0xFF;
                    minHue = (h + gap) & 0xFF;
                }
            }
            const int minS = minSat[size_t(minValue)], maxS = maxSat[size_t(minValue)];
            const long brightnessMasked = long(minValue) * (256 - kWorstSaturation) * (kWorstHueSpan + 16);
            const long hsvMasked = long((maxHue + 1 - minHue) & 0xFF) * (maxValue + 1 - minValue) * (maxS + 1 - minS);
            if (brightnessMasked + hsvMasked < bestMasked) {
                bestMasked = brightnessMasked + hsvMasked;
                bestMinValue = minValue;
                bestMinHue = minHue;
                bestMaxHue = maxHue;
                bestMinSat = minS;
                bestMaxSat = maxS;
                bestMaxValue = maxValue;
            }
        }
        const int hueSpan = (bestMaxHue - bestMinHue) & 0xFF;
        if (hueSpan > kWorstHueSpan) {
            // Not consistent: the best span of the widest allowed.
            long bestSum = 0;
            const int radius = kWorstHueSpan / 2;
            for (int h = 0; h < 256; ++h) {
                long sum = 0;
                for (int span = 0; span <= kWorstHueSpan; ++span) {
                    const int d = span - radius;
                    sum += long(radius * radius - d * d) * hueValue[size_t(((h + span) & 0xFF) * 256 + bestMinValue)];
                }
                if (sum > bestSum) {
                    bestSum = sum;
                    bestMinHue = h;
                    bestMaxHue = (h + kWorstHueSpan) & 0xFF;
                }
            }
        }
        if (bestMinSat > bestMaxSat || bestMinValue > bestMaxValue) {
            // Not a pixel in the range was saturated enough.
            bestMinSat = kWorstSaturation - 1;
            bestMaxSat = 255;
            bestMinValue = kMinMaskValue - 1;
            bestMaxValue = kMinMaskValue - 1;
        }
        out.minHue = bestMinHue;
        out.maxHue = bestMaxHue;
        out.minSaturation = bestMinSat;
        out.maxSaturation = bestMaxSat;
        out.minValue = bestMinValue;
        out.maxValue = bestMaxValue;
        // The problems marked in the key colour's average hue, vivid (OpenPnP's signal colour).
        const int avgHue = bestMinHue > bestMaxHue ? (bestMinHue + bestMaxHue) / 2 : ((255 + bestMinHue + bestMaxHue) / 2) & 0xFF;
        problemsOf(
            [&](int h, int s, int v) {
                return v >= bestMinValue
                    && (v > bestMaxValue || s < bestMinSat || s > bestMaxSat
                        || (bestMinHue < bestMaxHue ? h > bestMaxHue || h < bestMinHue : h > bestMaxHue && h < bestMinHue));
            },
            bgrOf(avgHue, 255, 255));
        // In words.
        std::string& r = out.diagnostics;
        r = "Non-color-keyed background elements are ";
        if (bestMinValue > kWorstValue)
            r += "too bright.\nTry to eliminate highlights and reflections.\nUse a shade behind the nozzle.\nRenew the "
                 "blackening of dark parts of the nozzle tip.\nClean the nozzle tip. If it is shiny, make it dull.\n"
                 "Eliminate light sources that reflect on the nozzle tip.";
        else if (bestMinValue > kWorstValue / 2) r += "sufficiently dark.\n";
        else if (bestMinValue <= kMinMaskValue) r += "possibly too dark.\nCheck camera exposure.\n";
        else r += "quite dark. Perfect!\n";
        if (bestMinValue <= kWorstValue) {
            r += "\nThe key color is ";   // OpenPnP's <hr/>: a paragraph of its own
            if (hueSpan > kWorstHueSpan)
                r += "not consistent enough.\nCheck camera white balance.\nClean the nozzle tip. If it is shiny, make it "
                     "dull.\nEliminate light sources that reflect on the nozzle tip.\n";
            else if (hueSpan > kWorstHueSpan / 2) r += "sufficiently consistent.\n";
            else if (hueSpan > 1) r += "very consistent. Perfect!\n";
            else r += "not detectable.\n";
            if (hueSpan <= kWorstHueSpan) {
                r += "\nThe key color is ";   // OpenPnP's <hr/>: a paragraph of its own
                if (bestMinSat < kWorstSaturation)
                    r += "not vivid enough.\nCheck camera white balance.\nClean the nozzle tip. If it is shiny, make it "
                         "dull.\nEliminate light sources that reflect on the nozzle tip.\n";
                else if (bestMinSat < (255 + kWorstSaturation) / 2) r += "sufficiently saturated.\n";
                else r += "very vivid. Perfect!\n";
            }
        }
    } else {
        // Brightness: the value only.
        int bestMinValue = 255, bestMaxValue = 0;
        for (const cv::Mat& img : m_images) {
            if (img.rows != rows || img.cols != cols) continue;
            each(img, [&](int, int, int, int, int v) {
                bestMaxValue = std::max(bestMaxValue, v);
                // 0 left out of the black point: it may be an undefined pixel (a transform's, a calibration's).
                if (v > 0) bestMinValue = std::min(bestMinValue, v);
            });
        }
        problemsOf([](int, int, int v) { return v > kWorstValue; }, cv::Vec3b(255, 0, 255));   // magenta, as OpenPnP's
        if (bestMinValue == 1) bestMinValue = 0;   // the black point all the way down
        out.minHue = 0;
        out.maxHue = 255;
        out.minSaturation = 0;
        out.maxSaturation = 255;
        out.minValue = bestMinValue;
        out.maxValue = bestMaxValue;
        std::string& r = out.diagnostics;
        r = "Background elements are ";
        if (bestMaxValue > kWorstValue)
            r += "too bright.\nTry to eliminate highlights and reflections.\nUse a shade behind the nozzle.\nRenew the "
                 "blackening of dark parts of the nozzle tip.\nClean the nozzle tip. If it is shiny, make it dull.\n"
                 "Eliminate light sources that reflect on the nozzle tip.";
        else if (bestMaxValue > kWorstValue / 2) r += "sufficiently dark.\n";
        else if (bestMaxValue <= kMinMaskValue) r += "possibly too dark.\nCheck camera exposure.\n";
        else r += "quite dark. Perfect!\n";
    }
    return true;
}

} // inline namespace jf
