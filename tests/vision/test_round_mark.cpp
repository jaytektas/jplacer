// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// The round-mark finder against drawn pictures whose answers are known: a mark
// bright on dark and dark on bright, with noise, with light falling off across
// the picture (sun from one side), and beside a mark of the wrong size. It
// must land within a tenth of a pixel, and say why when the mark is not there.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "vision/JPRoundMarkFinder.h"

#include <cmath>
#include <random>

using namespace jf;

namespace {

// A picture W x H of `ground`, with a disc of `fill` at (cx, cy), radius r,
// edges anti-aliased by supersampling (as a lens blurs them).
void disc(JPGrayImage& img, double cx, double cy, double r, float fill) {
    constexpr int kSub = 4;
    for (int y = 0; y < img.height; ++y)
        for (int x = 0; x < img.width; ++x) {
            if (std::abs(x - cx) > r + 2 || std::abs(y - cy) > r + 2) continue;
            int inside = 0;
            for (int sy = 0; sy < kSub; ++sy)
                for (int sx = 0; sx < kSub; ++sx) {
                    const double px = x + (sx + 0.5) / kSub - 0.5 - cx, py = y + (sy + 0.5) / kSub - 0.5 - cy;
                    inside += px * px + py * py <= r * r;
                }
            const float a = float(inside) / (kSub * kSub);
            float& p = img.pixels[size_t(y) * size_t(img.width) + size_t(x)];
            p = p * (1 - a) + fill * a;
        }
}

JPGrayImage ground(int w, int h, float level) {
    JPGrayImage g;
    g.width = w;
    g.height = h;
    g.pixels.assign(size_t(w) * size_t(h), level);
    return g;
}

void noise(JPGrayImage& img, float sigma, unsigned seed) {
    std::mt19937 rng(seed);
    std::normal_distribution<float> n(0, sigma);
    for (float& p : img.pixels) p = std::clamp(p + n(rng), 0.f, 255.f);
}

// Light falling off from left to right: the sun at one side.
void sunFromLeft(JPGrayImage& img, float strength) {
    for (int y = 0; y < img.height; ++y)
        for (int x = 0; x < img.width; ++x)
            img.pixels[size_t(y) * size_t(img.width) + size_t(x)] *= 1 + strength * (1 - float(x) / float(img.width));
}

void expectAt(const JPGrayImage& img, double cx, double cy, double d, double startX, double startY) {
    JPRoundMarkFinder::Request rq;
    rq.expectedX = startX;
    rq.expectedY = startY;
    rq.searchRadius = 30;
    rq.diameter = d;
    const JPRoundMark m = JPRoundMarkFinder::find(img, rq);
    assert(m.found);
    assert(std::abs(m.x - cx) < 0.1 && std::abs(m.y - cy) < 0.1);
    assert(std::abs(m.diameter - d) < 2.0 && m.confidence > 0.5 && m.shape > 0.95);
}

} // namespace

int main() {
    const double cx = 120.37, cy = 95.81, d = 42;   // a 1.85 mm mark at about 0.044 mm a pixel

    // Bright on dark, a little off from where it was expected.
    JPGrayImage a = ground(240, 200, 40);
    disc(a, cx, cy, d / 2, 200);
    expectAt(a, cx, cy, d, cx + 12, cy - 9);

    // Dark on bright: the same answer, no setting changed.
    JPGrayImage b = ground(240, 200, 210);
    disc(b, cx, cy, d / 2, 30);
    expectAt(b, cx, cy, d, cx - 15, cy + 10);

    // Noise and sun from one side.
    JPGrayImage c = ground(240, 200, 60);
    disc(c, cx, cy, d / 2, 170);
    sunFromLeft(c, 0.8f);
    noise(c, 12, 7);
    expectAt(c, cx, cy, d, cx + 8, cy + 8);

    // A bigger round thing nearby is not the mark.
    JPGrayImage e = ground(320, 200, 40);
    disc(e, cx, cy, d / 2, 200);
    disc(e, cx + 120, cy, d, 200);   // twice the size, 120 px away
    expectAt(e, cx, cy, d, cx + 5, cy);

    // No mark at all: found is false, and it says why.
    JPGrayImage f = ground(240, 200, 80);
    noise(f, 8, 3);
    JPRoundMarkFinder::Request rq;
    rq.expectedX = 120;
    rq.expectedY = 100;
    rq.searchRadius = 30;
    rq.diameter = d;
    const JPRoundMark none = JPRoundMarkFinder::find(f, rq);
    assert(!none.found && !none.why.empty());

    // Only a mark of the wrong size there: refused, with the size in the reason.
    JPGrayImage g = ground(240, 200, 40);
    disc(g, cx, cy, d * 1.4 / 2, 200);   // 40% too big
    rq.expectedX = cx;
    rq.expectedY = cy;
    const JPRoundMark wrong = JPRoundMarkFinder::find(g, rq);
    assert(!wrong.found && wrong.why.find("expected") != std::string::npos);

    // One far bigger than asked for fills the whole search: nothing of the size asked is there.
    JPGrayImage h = ground(240, 200, 40);
    disc(h, cx, cy, d, 200);   // twice as big
    const JPRoundMark huge = JPRoundMarkFinder::find(h, rq);
    assert(!huge.found && huge.why.find("whole mark") != std::string::npos);
    return 0;
}
