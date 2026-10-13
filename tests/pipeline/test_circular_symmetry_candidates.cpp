// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// Circular symmetry with one target wanted: a small sharp fiducial, off the search's middle, beside a soft round
// blob near the search's edge that scores better on the coarse pass. The fiducial is found, not the blob: the
// best few of the coarse pass are searched finer, not only the best (bench, 2026-10-13: a 1 mm fiducial missed
// for bare board 51 px away).
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "pipeline/JPCircularSymmetry.h"

#include <cmath>
#include <cstdint>

using namespace jf;

namespace {

// The same picture every run, whatever the library: noise from a fixed linear congruential generator.
uint32_t g_state = 1;
int noise(int amplitude) {
    g_state = g_state * 1664525u + 1013904223u;
    return int((g_state >> 8) % uint32_t(2 * amplitude + 1)) - amplitude;
}

} // namespace

int main() {
    const int w = 320, h = 240;
    cv::Mat image(h, w, CV_8UC3);
    const double fx = w / 2 + 9.3, fy = h / 2 + 18.6;   // the fiducial
    const double bx = w / 2 - 43, by = h / 2 + 28;      // the blob, 51 px out
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            int bgr[3] = { 90, 110, 40 };   // the board's green
            const double d = std::hypot(x - fx, y - fy);
            const int fiducial = d < 9.5 ? 40 : d < 23 ? -25 : 0;   // its pad, its mask opening
            const double blob = 80 * std::exp(-((x - bx) * (x - bx) + (y - by) * (y - by)) / (2 * 7.0 * 7.0));
            const int n = noise(15);
            cv::Vec3b& px = image.at<cv::Vec3b>(y, x);
            for (int c = 0; c < 3; ++c) px[c] = uchar(std::clamp(int(bgr[c] + fiducial + blob + n), 0, 255));
        }
    JPCircularSymmetry::Search q;
    q.xCenter = w / 2;
    q.yCenter = h / 2;
    q.minDiameter = 15;
    q.maxDiameter = 31;
    q.searchDiameter = q.searchWidth = q.searchHeight = 2 * 51;
    q.subSampling = 8;
    q.superSampling = 8;
    JPCircularSymmetry::ScoreRange range;
    const auto found = JPCircularSymmetry::inPixels(JPCircularSymmetry::find(image, q, range));
    assert(found.size() == 1);
    assert(std::hypot(found.front().x - fx, found.front().y - fy) < 1.0);
    return 0;
}
