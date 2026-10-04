// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// A template cut from one picture found in another where the scene moved,
// brighter and with noise, to a fraction of a pixel; kept to the area of
// interest; and not found where it is not.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "vision/JPTemplateFinder.h"

#include <cmath>
#include <random>

using namespace jf;

namespace {

constexpr int kW = 320, kH = 240;

// A scene of a few blobs, moved by (dx, dy) pixels, `gain` brighter.
JPGrayImage scene(double dx, double dy, float gain, unsigned seed) {
    JPGrayImage img;
    img.width = kW;
    img.height = kH;
    img.pixels.resize(size_t(kW) * kH);
    std::mt19937 rng(seed);
    std::normal_distribution<float> noise(0, 3);
    const double blobs[][3] = { { 100, 80, 9 }, { 130, 95, 5 }, { 112, 120, 7 }, { 240, 180, 10 } };
    for (int y = 0; y < kH; ++y)
        for (int x = 0; x < kW; ++x) {
            double v = 40;
            for (const auto& b : blobs) {
                const double r = std::hypot(x - b[0] - dx, y - b[1] - dy);
                v += 150 * std::exp(-r * r / (2 * b[2] * b[2]));
            }
            img.pixels[size_t(y) * kW + size_t(x)] = float(v) * gain + noise(rng);
        }
    return img;
}

JPGrayImage cut(const JPGrayImage& img, int x0, int y0, int w, int h) {
    JPGrayImage t;
    t.width = w;
    t.height = h;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) t.pixels.push_back(img.at(x0 + x, y0 + y));
    return t;
}

} // namespace

int main() {
    const JPGrayImage before = scene(0, 0, 1, 1);
    const JPGrayImage templ = cut(before, 80, 60, 60, 80);
    const JPGrayImage after = scene(7.4, -4.6, 1.3f, 2);

    const auto r = JPTemplateFinder::find(after, templ, {});
    assert(r.found);
    assert(std::abs(r.x - (80 + 7.4)) < 0.5 && std::abs(r.y - (60 - 4.6)) < 0.5);
    assert(r.score > 0.9);

    // Within an area that holds it.
    const auto in = JPTemplateFinder::find(after, templ, { 50, 30, 120, 130 });
    assert(in.found && std::abs(in.x - r.x) < 1e-9 && std::abs(in.y - r.y) < 1e-9);

    // An area where it is not: nothing found, and why.
    const auto away = JPTemplateFinder::find(after, templ, { 200, 0, 120, 100 });
    assert(!away.found && !away.why.empty());

    // A template larger than the area.
    assert(!JPTemplateFinder::find(after, templ, { 0, 0, 40, 40 }).found);
    return 0;
}
