// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// A part found by its pads in a picture from below: an 0603's two pads and
// an SOIC-8's eight, drawn off centre and turned, with noise, found to a
// fraction of a pixel and a degree; and nothing found where there is no part.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "vision/JPPartFinder.h"

#include <cmath>
#include <cstdio>
#include <random>

using namespace jf;

namespace {

constexpr double kPxPerMm = 30;
constexpr int    kW = 640, kH = 480;

// The picture: dark, the pads bright, the part at (mx, my) mm off the middle, turned `angle`.
JPGrayImage picture(const std::vector<JPPartFinder::Rect>& pads, double mx, double my, double angle, unsigned seed) {
    JPGrayImage img;
    img.width = kW;
    img.height = kH;
    img.pixels.assign(size_t(kW) * kH, 30.f);
    std::mt19937 rng(seed);
    std::normal_distribution<float> noise(0, 6);
    const double t = -angle * M_PI / 180;
    for (int y = 0; y < kH; ++y)
        for (int x = 0; x < kW; ++x) {
            // Pixel to machine: y up on the machine is down in the picture.
            const double px = (x - kW / 2.0) / kPxPerMm - mx, py = -(y - kH / 2.0) / kPxPerMm - my;
            const double u = px * std::cos(t) - py * std::sin(t), v = px * std::sin(t) + py * std::cos(t);
            bool on = false;
            for (const auto& r : pads)
                on = on || (std::abs(u - r.x) <= r.width / 2 && std::abs(v - r.y) <= r.height / 2);
            img.pixels[size_t(y) * kW + size_t(x)] = (on ? 200.f : 30.f) + noise(rng);
        }
    return img;
}

JPPartFinder::Request request(double angle) {
    JPPartFinder::Request rq;
    rq.expectedX = kW / 2.0;
    rq.expectedY = kH / 2.0;
    rq.angle = angle;
    rq.angleRange = 10;
    rq.searchMm = 1.5;
    rq.toMachine = [](double px, double py, double& mx, double& my) {
        mx = (px - kW / 2.0) / kPxPerMm;
        my = -(py - kH / 2.0) / kPxPerMm;
        return true;
    };
    return rq;
}

} // namespace

int main() {
    // An 0603: two 0.9 x 0.95 mm pads 1.6 mm apart.
    const std::vector<JPPartFinder::Rect> r0603 { { -0.8, 0, 0.9, 0.95, 0 }, { 0.8, 0, 0.9, 0.95, 0 } };
    {
        const JPGrayImage img = picture(r0603, 0.3, -0.2, 93.5, 1);
        const auto r = JPPartFinder::find(img, r0603, request(90));
        std::printf("0603: %.3f, %.3f px, %.2f deg, score %.2f %s\n", r.x, r.y, r.angle, r.score, r.why.c_str());
        std::fflush(stdout);
        assert(r.found);
        assert(std::abs(r.x - (kW / 2.0 + 0.3 * kPxPerMm)) < 1.5 && std::abs(r.y - (kH / 2.0 + 0.2 * kPxPerMm)) < 1.5);
        // Symmetric: 93.5 or 273.5; the one nearest 90.
        assert(std::abs(r.angle - 93.5) < 0.8);
    }
    // An SOIC-8: eight 1.5 x 0.6 pads, 1.27 pitch, 5.4 apart.
    std::vector<JPPartFinder::Rect> soic;
    for (int i = 0; i < 4; ++i) {
        soic.push_back({ -2.7, 1.905 - 1.27 * i, 1.5, 0.6, 0 });
        soic.push_back({ 2.7, 1.905 - 1.27 * i, 1.5, 0.6, 0 });
    }
    {
        const JPGrayImage img = picture(soic, -0.5, 0.4, -4, 2);
        const auto r = JPPartFinder::find(img, soic, request(0));
        std::printf("SOIC-8: %.3f, %.3f px, %.2f deg, score %.2f %s\n", r.x, r.y, r.angle, r.score, r.why.c_str());
        std::fflush(stdout);
        assert(r.found);
        assert(std::abs(r.x - (kW / 2.0 - 0.5 * kPxPerMm)) < 1.5 && std::abs(r.y - (kH / 2.0 - 0.4 * kPxPerMm)) < 1.5);
        assert(std::abs(r.angle + 4) < 0.6);
    }
    // No part: nothing found.
    {
        const JPGrayImage img = picture({}, 0, 0, 0, 3);
        const auto r = JPPartFinder::find(img, soic, request(0));
        assert(!r.found);
    }
    return 0;
}
