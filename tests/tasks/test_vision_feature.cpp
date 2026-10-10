// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// OpenPnP's VisionSolutions feature detection: a light paper dot 120 pixels across a little off the middle of a
// noisy picture, with a smaller dot (40 pixels) further out, among rows of bright pads round the edges as on a board
// (their edges give the search a score of its own where it finds no circle). Detected at about its diameter, it is found where it
// is, its diameter measured, and it scores; asked for at a quarter of its size it is not taken; with diagnostics
// the picture is drawn on. Auto-Detect Next from small sizes up comes to it (the smaller dot is outside the search),
// and its next after it is none bigger in the picture (round again to it); the first press from the smallest size
// comes straight to it, not to a size where nothing is found (the search's noise score taken for a peak).
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "tasks/JPVisionFeature.h"

#include <opencv2/imgproc.hpp>

#include <cmath>
#include <cstdio>
#include <random>

using namespace jf;

namespace {

cv::Mat picture() {
    cv::Mat bgr(480, 640, CV_8UC3, cv::Scalar(60, 70, 65));
    cv::circle(bgr, cv::Point(335, 228), 60, cv::Scalar(230, 235, 235), cv::FILLED, cv::LINE_AA);
    cv::circle(bgr, cv::Point(560, 400), 20, cv::Scalar(230, 235, 235), cv::FILLED, cv::LINE_AA);
    for (int k = 0; k < 40; ++k) {
        const int along = 20 + k * 15;
        cv::rectangle(bgr, cv::Rect(along, 8, 7, 34), cv::Scalar(240, 245, 245), cv::FILLED);
        cv::rectangle(bgr, cv::Rect(along, 438, 7, 34), cv::Scalar(240, 245, 245), cv::FILLED);
    }
    std::mt19937 rng(3);
    std::normal_distribution<double> noise(0, 4);
    for (int y = 0; y < bgr.rows; ++y)
        for (int x = 0; x < bgr.cols; ++x) {
            cv::Vec3b& p = bgr.at<cv::Vec3b>(y, x);
            const double n = noise(rng);
            for (int c = 0; c < 3; ++c) p[c] = cv::saturate_cast<uchar>(p[c] + n);
        }
    return bgr;
}

} // namespace

int main() {
    const cv::Mat base = picture();
    {
        cv::Mat p = base.clone();
        double score = 0;
        const auto f = JPVisionFeature::detect(p, 115, 0, false, false, score);
        assert(f);
        std::fprintf(stderr, "found %.2f %.2f d %.1f score %.2f\n", f->x, f->y, f->diameter, f->score);
        assert(std::abs(f->x - 335.5) < 1.5 && std::abs(f->y - 228.5) < 1.5 && std::abs(f->diameter - 120) < 6 && score > 1.5);
        assert(p.at<cv::Vec3b>(0, 0) == base.at<cv::Vec3b>(0, 0));   // no diagnostics: not drawn on (away from it)
    }
    {
        cv::Mat p = base.clone();
        double score = 0;
        const auto f = JPVisionFeature::detect(p, 30, 0, true, true, score);
        std::fprintf(stderr, "at 30 px: %s score %.2f\n", f ? "found" : "none", score);
        assert(!f || std::abs(f->diameter - 30) < 8);   // whatever it takes is about the size asked
        assert(cv::norm(p, base, cv::NORM_L1) > 0);     // diagnostics drawn
    }
    assert(JPVisionFeature::maxDiameter(640, 480) == 336);
    // Pressed again and again, as OpenPnP's Auto-Detect Next is: it comes to the dot.
    int at = JPVisionFeature::kFirstTriedPx;
    bool reached = false;
    for (int press = 0; press < 8 && !reached; ++press) {
        const auto next = JPVisionFeature::next(base, at);
        std::fprintf(stderr, "next from %d: %d\n", at, next ? *next : -1);
        assert(next);
        reached = std::abs(*next - 120) < 8;
        if (*next <= at) break;   // round again
        at = *next;
    }
    assert(reached);
    // The first press from the smallest size: the dot, at once.
    const auto first = JPVisionFeature::next(base, JPVisionFeature::kFirstTriedPx);
    std::fprintf(stderr, "first press: %d\n", first ? *first : -1);
    assert(first && std::abs(*first - 120) < 8);
    return 0;
}
