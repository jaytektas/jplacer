// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

inline namespace jf {

// A greyscale picture for measuring: one float a pixel (0..255), rows top to
// bottom. Sampled between pixels by bilinear interpolation, so a measurement
// can land on a fraction of a pixel.
struct JPGrayImage {
    int                width = 0, height = 0;
    std::vector<float> pixels;

    float at(int x, int y) const { return pixels[size_t(y) * size_t(width) + size_t(x)]; }

    // Bilinear sample; nothing outside the picture (the caller skips it).
    bool sample(float x, float y, float& out) const {
        if (x < 0 || y < 0 || x > float(width - 1) || y > float(height - 1)) return false;
        const int x0 = std::min(int(x), width - 2), y0 = std::min(int(y), height - 2);
        const float fx = x - float(x0), fy = y - float(y0);
        const float a = at(x0, y0), b = at(x0 + 1, y0), c = at(x0, y0 + 1), d = at(x0 + 1, y0 + 1);
        out = (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy;
        return true;
    }

    // What `lit` has that `unlit` has not, pixel by pixel (never below 0):
    // the same view with a light on and off, the room's own light taken out.
    // Empty when the two are not the same size.
    static JPGrayImage difference(const JPGrayImage& lit, const JPGrayImage& unlit) {
        JPGrayImage d;
        if (lit.width != unlit.width || lit.height != unlit.height) return d;
        d.width = lit.width;
        d.height = lit.height;
        d.pixels.resize(lit.pixels.size());
        for (size_t i = 0; i < d.pixels.size(); ++i) d.pixels[i] = std::max(0.f, lit.pixels[i] - unlit.pixels[i]);
        return d;
    }

    // Half the size each way, each pixel the mean of the four it covers (a
    // coarse copy to search before measuring on the full picture).
    JPGrayImage halved() const {
        JPGrayImage h;
        h.width  = width / 2;
        h.height = height / 2;
        h.pixels.resize(size_t(h.width) * size_t(h.height));
        for (int y = 0; y < h.height; ++y)
            for (int x = 0; x < h.width; ++x)
                h.pixels[size_t(y) * size_t(h.width) + size_t(x)] =
                    0.25f * (at(2 * x, 2 * y) + at(2 * x + 1, 2 * y) + at(2 * x, 2 * y + 1) + at(2 * x + 1, 2 * y + 1));
        return h;
    }

    // From RGBA (what a camera frame is): luminance, BT.601 weights.
    static JPGrayImage fromRgba(const uint8_t* rgba, int width, int height) {
        JPGrayImage g;
        g.width = width;
        g.height = height;
        g.pixels.resize(size_t(width) * size_t(height));
        for (size_t i = 0; i < g.pixels.size(); ++i, rgba += 4)
            g.pixels[i] = 0.299f * rgba[0] + 0.587f * rgba[1] + 0.114f * rgba[2];
        return g;
    }
};

} // inline namespace jf
