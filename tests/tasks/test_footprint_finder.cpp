// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// A footprint's pads found in a picture (JPFootprintFinder): two 0603 pads on dark solder mask, with silkscreen and a
// neighbouring footprint's pads nearby and a little noise, drawn off where they were expected by a fraction of a
// pixel: found within a tenth of a pixel of it; turned a quarter turn, the same; nothing like them there (bare mask,
// or only silkscreen), not found.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "tasks/JPFootprintFinder.h"

#include <opencv2/imgproc.hpp>

#include <cmath>
#include <random>

using namespace jf;

namespace {

// Two pads `w` x `h` px, centres `pitch` apart along X (turned 90: along Y), about (cx, cy).
std::vector<std::vector<cv::Point2d>> pads(double cx, double cy, double w, double h, double pitch, bool turned) {
    std::vector<std::vector<cv::Point2d>> out;
    for (int side : { -1, 1 }) {
        double px = cx + side * pitch / 2, py = cy, hw = w / 2, hh = h / 2;
        if (turned) {
            px = cx;
            py = cy + side * pitch / 2;
            std::swap(hw, hh);
        }
        out.push_back({ { px - hw, py - hh }, { px + hw, py - hh }, { px + hw, py + hh }, { px - hw, py + hh } });
    }
    return out;
}

// A picture of solder mask with the pads at their place, a neighbour 40 px right, a silkscreen outline round
// them, and noise; drawn at 8x and shrunk, so a fraction of a pixel shows.
cv::Mat picture(const std::vector<std::vector<cv::Point2d>>& at, bool withPads) {
    constexpr int kUp = 8;
    cv::Mat big(240 * kUp, 320 * kUp, CV_8UC3, cv::Scalar(60, 90, 40));
    auto fill = [&](const std::vector<std::vector<cv::Point2d>>& ps, double ox, cv::Scalar colour) {
        std::vector<std::vector<cv::Point>> polys;
        for (const auto& p : ps) {
            std::vector<cv::Point> q;
            // A small pixel's centre is the middle of its kUp big ones.
            for (const auto& c : p)
                q.emplace_back(int(std::lround((c.x + ox + 0.5) * kUp - 0.5)), int(std::lround((c.y + 0.5) * kUp - 0.5)));
            polys.push_back(q);
        }
        cv::fillPoly(big, polys, colour);
    };
    if (withPads) fill(at, 0, cv::Scalar(200, 205, 210));
    fill(at, 40, cv::Scalar(200, 205, 210));   // the neighbour
    cv::rectangle(big, cv::Rect(138 * kUp, 92 * kUp, 48 * kUp, 58 * kUp), cv::Scalar(235, 235, 235), 2 * kUp);   // silkscreen
    cv::Mat small;
    cv::resize(big, small, cv::Size(320, 240), 0, 0, cv::INTER_AREA);
    cv::Mat noise(small.size(), CV_16SC3);
    cv::randn(noise, 0, 6);
    cv::Mat out;
    small.convertTo(out, CV_16SC3);
    out += noise;
    out.convertTo(out, CV_8UC3);
    return out;
}

} // namespace

int main() {
    for (bool turned : { false, true }) {
        // Expected about (160, 120); really there 2.3 px right and 1.6 px down.
        const auto expected = pads(160, 120, 10, 12, 22, turned);
        const auto real = pads(162.3, 121.6, 10, 12, 22, turned);
        const cv::Mat bgr = picture(real, true);
        const JPFootprintFinder::Result r = JPFootprintFinder::find(bgr, expected, 13, 0.5);
        assert(r.found && r.score > 0.8);
        assert(std::abs(r.dx - 2.3) < 0.15 && std::abs(r.dy - 1.6) < 0.15);
        // Not there: the mask and the silkscreen alone (the neighbour beyond the search) are not taken.
        const JPFootprintFinder::Result none = JPFootprintFinder::find(picture(real, false), expected, 13, 0.5);
        assert(!none.found);
    }
    return 0;
}
