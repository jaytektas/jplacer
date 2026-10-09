// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// A template cut from one colour picture found in another where the scene moved,
// brighter and with noise, to the pixel, as OpenPnP's locateTemplateMatches finds
// it; kept to the area of interest, the best place there taken (OpenPnP turns
// none away); a template larger than the area not looked for. A Neoden 4 feeder's
// area placed from the middle.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "pipeline/JPTemplateFinder.h"

#include <opencv2/core.hpp>

#include <cmath>
#include <cstdlib>
#include <random>

using namespace jf;

namespace {

constexpr int kW = 320, kH = 240;

// A scene of a few coloured blobs, moved by (dx, dy) pixels, `gain` brighter.
cv::Mat scene(double dx, double dy, float gain, unsigned seed) {
    cv::Mat img(kH, kW, CV_8UC3);
    std::mt19937 rng(seed);
    std::normal_distribution<float> noise(0, 3);
    const double blobs[][3] = { { 100, 80, 9 }, { 130, 95, 5 }, { 112, 120, 7 }, { 240, 180, 10 } };
    const float tint[] = { 0.6f, 1.0f, 0.8f };
    for (int y = 0; y < kH; ++y)
        for (int x = 0; x < kW; ++x) {
            double v = 40;
            for (const auto& b : blobs) {
                const double r = std::hypot(x - b[0] - dx, y - b[1] - dy);
                v += 150 * std::exp(-r * r / (2 * b[2] * b[2]));
            }
            cv::Vec3b& p = img.at<cv::Vec3b>(y, x);
            for (int c = 0; c < 3; ++c) p[c] = cv::saturate_cast<uint8_t>(float(v) * gain * tint[c] + noise(rng));
        }
    return img;
}

} // namespace

int main() {
    const cv::Mat before = scene(0, 0, 1, 1);
    const cv::Mat templ = before(cv::Rect(80, 60, 60, 80)).clone();
    const cv::Mat after = scene(7, -5, 1.3f, 2);

    const auto r = JPTemplateFinder::find(after, templ, {});
    assert(r.found);
    assert(std::abs(r.x - (80 + 7)) <= 1 && std::abs(r.y - (60 - 5)) <= 1);

    // Within an area that holds it: the same place.
    const auto in = JPTemplateFinder::find(after, templ, { 50, 30, 120, 130 });
    assert(in.found && in.x == r.x && in.y == r.y);

    // An area where it is not: the best place there, within it.
    const auto away = JPTemplateFinder::find(after, templ, { 200, 0, 120, 100 });
    assert(away.found && away.x >= 200 && away.x + templ.cols <= 320 && away.y >= 0 && away.y + templ.rows <= 100);

    // A template larger than the area, or not in colour.
    assert(!JPTemplateFinder::find(after, templ, { 0, 0, 40, 40 }).found);
    cv::Mat grey(80, 60, CV_8UC1, cv::Scalar(0));
    assert(!JPTemplateFinder::find(after, grey, {}).found);
    // A Neoden 4 feeder's area: from the middle, each kept within 0..512.
    const JPTemplateFinder::Area n = JPTemplateFinder::Area { -50, -40, 100, 600, true }.placed(640, 480);
    assert(n.x == 270 && n.y == 200 && n.width == 100 && n.height == 512 && !n.fromMiddle);
    const JPTemplateFinder::Area far = JPTemplateFinder::Area { 300, -400, 10, 10, true }.placed(640, 480);
    assert(far.x == 512 && far.y == 0);
    return 0;
}
