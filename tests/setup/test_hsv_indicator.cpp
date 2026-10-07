// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// JPHsvIndicator, OpenPnP's HsvIndicator: a colour wheel of the background calibration's HSV ranges, and a bar
// of its values. In the hue and saturation ranges, the wheel bright in its own colour; outside them, dimmed to
// the least value; disabled, without colour; outside the wheel and the bar, transparent.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "setup/JPHsvIndicator.h"

#include <cmath>
#include <cstdint>

using namespace jf;

namespace {

const uint8_t* at(const JPFrame& f, int x, int y) { return f.rgba.data() + (size_t(y) * size_t(f.width) + size_t(x)) * 4; }

} // namespace

int main() {
    // OpenPnP's screenshot of a green-blue key colour: hue 123..141 (of 255), saturation 154..255, value 32..237.
    const JPHsvIndicator::Ranges r { 123, 141, 154, 255, 32, 237 };
    const auto f = JPHsvIndicator::picture(r, true);
    assert(f->width == JPHsvIndicator::kWidth && f->height == JPHsvIndicator::kHeight);
    const int d = std::min(f->width, f->height);
    const double radius = d / 2.0;
    // A point on the wheel at hue h (0..255, counter-clockwise from the right), a share of the radius out.
    auto wheel = [&](double h, double out) {
        const double a = h / 255.0 * 2 * M_PI;
        return at(*f, int(radius + std::cos(a) * radius * out), int(radius - std::sin(a) * radius * out));
    };
    // In range (hue 132, near the rim): cyan, bright, opaque.
    const uint8_t* in = wheel(132, 0.85);
    assert(in[3] == 255 && in[2] > 150 && in[1] > 150 && in[0] < 100);
    // Out of range (hue 0, red): dimmed to the least value (32).
    const uint8_t* out = wheel(0, 0.85);
    assert(out[0] <= 40 && out[1] <= 40 && out[2] <= 40);
    // Outside the wheel (top right corner of its square): transparent.
    assert(at(*f, d - 1, 0)[3] == 0);
    // The bar: past the wheel by a fifth of it; at the top (value 255, above the range) faded, in the middle opaque.
    const int barX = d + d / 5 + 2;
    assert(at(*f, barX, 0)[3] < 255);
    assert(at(*f, barX, d / 2)[3] == 255);
    // Disabled: the wheel without colour.
    const auto g = JPHsvIndicator::picture(r, false);
    const uint8_t* grey = at(*g, int(radius + std::cos(132 / 255.0 * 2 * M_PI) * radius * 0.85),
                             int(radius - std::sin(132 / 255.0 * 2 * M_PI) * radius * 0.85));
    assert(grey[0] == grey[1] && grey[1] == grey[2]);
    return 0;
}
