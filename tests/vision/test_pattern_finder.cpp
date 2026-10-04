// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// A pattern found in a picture by normalised cross-correlation: an SOIC-8's
// pads drawn where they should look, found at a known place to a fraction of
// a pixel through noise and a change of brightness; nothing found where
// there is nothing like it; the search kept to its radius.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "vision/JPPatternFinder.h"

#include <cmath>
#include <random>

using namespace jf;

namespace {

// Pads (as rectangles, mm) drawn at `scale` px/mm round (cx, cy), antialiased
// by 4x4 supersampling: bright pads on a darker ground.
JPGrayImage draw(int w, int h, double cx, double cy, double scale, double ground, double pad, unsigned seed, double noise) {
    struct R { double x, y, w, h; };
    std::vector<R> pads;
    for (int i = 0; i < 4; ++i) {
        pads.push_back({ -2.475, 1.905 - i * 1.27, 1.95, 0.6 });
        pads.push_back({ 2.475, 1.905 - i * 1.27, 1.95, 0.6 });
    }
    JPGrayImage img;
    img.width = w;
    img.height = h;
    img.pixels.resize(size_t(w) * size_t(h));
    std::mt19937 rng(seed);
    std::normal_distribution<double> n(0, noise);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            int in = 0;
            for (int sy = 0; sy < 4; ++sy)
                for (int sx = 0; sx < 4; ++sx) {
                    const double mx = (x + (sx + 0.5) / 4 - cx) / scale, my = -(y + (sy + 0.5) / 4 - cy) / scale;
                    for (const R& r : pads)
                        if (std::abs(mx - r.x) <= r.w / 2 && std::abs(my - r.y) <= r.h / 2) {
                            ++in;
                            break;
                        }
                }
            img.pixels[size_t(y) * size_t(w) + size_t(x)] = float(ground + (pad - ground) * in / 16.0 + n(rng));
        }
    return img;
}

} // namespace

int main() {
    const double scale = 20;   // px per mm
    // The pattern: the pads as they should look, centred.
    const JPGrayImage pattern = draw(140, 120, 70, 60, scale, 0, 1, 1, 0);
    // The picture: darker mask, dimmer copper, noise, somewhere else.
    const JPGrayImage picture = draw(400, 300, 212.3, 141.7, scale, 60, 150, 7, 6);

    JPPatternFinder::Request rq;
    rq.expectedX = 200;
    rq.expectedY = 150;
    rq.searchRadius = 40;
    const auto r = JPPatternFinder::find(picture, pattern, 70, 60, rq);
    assert(r.found && r.score > 0.9);
    assert(std::abs(r.x - 212.3) < 0.25 && std::abs(r.y - 141.7) < 0.25);

    // Searched too narrowly: not found there.
    rq.searchRadius = 5;
    assert(!JPPatternFinder::find(picture, pattern, 70, 60, rq).found);

    // Nothing like it: a plain noisy picture.
    const JPGrayImage blank = draw(400, 300, -1000, -1000, scale, 60, 150, 9, 6);
    rq.searchRadius = 40;
    const auto none = JPPatternFinder::find(blank, pattern, 70, 60, rq);
    assert(!none.found && !none.why.empty());
    return 0;
}
