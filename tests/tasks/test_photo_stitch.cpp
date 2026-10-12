// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// A board's photo made from pictures (JPPhotoStitch): pictures cut from a known bed, one placed 0.3 mm from where it
// was taken, stitched onto a board lying at an angle: the photo is the bed as the board sees it, the stray picture
// lined up by what it shares with its neighbours (and how far it was moved said); a shift between two pictures
// measured both ways round; the area no picture covers left clear.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "tasks/JPPhotoStitch.h"

#include <opencv2/imgproc.hpp>

#include <cmath>
#include <random>

using namespace jf;

namespace {

constexpr double kRes = 0.05;               // mm a pixel, the bed's and the pictures'
constexpr int    kBedW = 2000, kBedH = 1600; // 100 x 80 mm, machine (0, 80) at its top-left
constexpr int    kTileW = 400, kTileH = 300;

// The bed: blurred noise and bright marks, colour in each channel.
cv::Mat bed() {
    std::mt19937 rng(7);
    cv::Mat noise(kBedH, kBedW, CV_8UC3);
    cv::randu(noise, 0, 255);
    cv::Mat b;
    cv::GaussianBlur(noise, b, { 0, 0 }, 3);
    std::uniform_int_distribution<int> x(0, kBedW), y(0, kBedH), r(4, 30);
    for (int i = 0; i < 600; ++i)
        cv::circle(b, { x(rng), y(rng) }, r(rng), cv::Scalar(255 * (i % 3 == 0), 255 * (i % 3 == 1), 255), cv::FILLED);
    return b;
}

// A machine place's pixel on the bed.
cv::Point2d onBed(double x, double y) { return { x / kRes, (80 - y) / kRes }; }

// A picture centred on (cx, cy) as taken (exact pixels), and where it truly is.
JPPhotoStitch::Tile picture(const cv::Mat& b, double cx, double cy) {
    const cv::Point2d tl = onBed(cx - kTileW / 2 * kRes, cy + kTileH / 2 * kRes);
    JPPhotoStitch::Tile t;
    t.bgr = b(cv::Rect(int(std::lround(tl.x)), int(std::lround(tl.y)), kTileW, kTileH)).clone();
    t.toMachine.m00 = kRes;
    t.toMachine.m11 = -kRes;
    t.toMachine.m02 = cx - kTileW / 2 * kRes;
    t.toMachine.m12 = cy + kTileH / 2 * kRes;
    return t;
}

} // namespace

int main() {
    const cv::Mat b = bed();
    // The board at (20, 15), turned 10°.
    JPAffineTransform boardToMachine;
    boardToMachine.translate(20, 15);
    boardToMachine.rotate(10 * M_PI / 180);
    const JPPhotoStitch::Area area { 0, 0, 50, 40 };
    // Pictures every 15 x 11.25 mm (a quarter overlapping) over it.
    std::vector<JPPhotoStitch::Tile> tiles;
    for (double y = 12; y <= 62; y += 11.25)
        for (double x = 11; x <= 75; x += 15) tiles.push_back(picture(b, x, y));
    // Two pictures' shift both ways round.
    {
        JPPhotoStitch::Tile a = tiles[0], c = tiles[1];
        c.toMachine.m02 += 0.25;   // said to lie 0.25 mm right of where it is: its content seen 0.25 mm right
        double dx = 0, dy = 0, response = 0;
        assert(JPPhotoStitch::shiftBetween(a, c, kRes, dx, dy, response));
        assert(std::abs(dx - 0.25) < 0.02 && std::abs(dy) < 0.02 && response > JPPhotoStitch::kMinResponse);
        assert(JPPhotoStitch::shiftBetween(c, a, kRes, dx, dy, response) && std::abs(dx + 0.25) < 0.02);
        // Far apart: no overlap.
        assert(!JPPhotoStitch::shiftBetween(tiles[0], tiles.back(), kRes, dx, dy, response));
    }
    // One picture placed 0.3 mm off.
    const size_t stray = tiles.size() / 2;
    tiles[stray].toMachine.m02 += 0.3;
    JPPhotoStitch::Result r;
    std::string why;
    assert(JPPhotoStitch::stitch(tiles, boardToMachine, area, kRes, r, why));
    assert(r.bgra.cols == 1000 && r.bgra.rows == 800 && r.mmPerPixel == kRes);
    assert(r.lined > 0 && r.lined <= r.pairs);
    assert(r.largestCorrectionMm > 0.2 && r.largestCorrectionMm < 0.4 && r.largestDisagreementMm < JPPhotoStitch::kDisagreementToSayMm);
    assert(r.uncoveredShare < JPPhotoStitch::kUncoveredToSay);
    // The photo is the bed where the board lies: every pixel (but the edges') against the bed there.
    double diff = 0;
    long long n = 0;
    for (int row = 10; row < r.bgra.rows - 10; row += 3)
        for (int col = 10; col < r.bgra.cols - 10; col += 3) {
            const cv::Vec4b p = r.bgra.at<cv::Vec4b>(row, col);
            assert(p[3] == 255);
            double mx = 0, my = 0;
            boardToMachine.apply(r.originX + col * kRes, r.originY - row * kRes, mx, my);
            const cv::Point2d at = onBed(mx, my);
            cv::Mat one;
            cv::getRectSubPix(b, { 1, 1 }, cv::Point2f(float(at.x - 0.5), float(at.y - 0.5)), one);
            const cv::Vec3b q = one.at<cv::Vec3b>(0, 0);
            for (int k = 0; k < 3; ++k) diff += std::abs(int(p[k]) - int(q[k]));
            n += 3;
        }
    assert(diff / double(n) < 4);
    // The area reaching past the pictures: left clear, the share said.
    JPPhotoStitch::Result wide;
    assert(JPPhotoStitch::stitch(tiles, boardToMachine, { 0, 0, 50, 70 }, kRes, wide, why));   // past the pictures above
    assert(wide.uncoveredShare > 0.1 && wide.bgra.at<cv::Vec4b>(0, 0)[3] == 0);
    // Nothing to make it of.
    assert(!JPPhotoStitch::stitch({}, boardToMachine, area, kRes, r, why) && !why.empty());
    return 0;
}
