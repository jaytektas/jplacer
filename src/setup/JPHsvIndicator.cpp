// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPHsvIndicator.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

inline namespace jf {

namespace {

// How much of a value outside the range still shows on the bar (OpenPnP's EXCLUDED_RATIO).
constexpr double kExcludedRatio = 0.33;

// Java's Color.getHSBColor (each 0..255 here), with alpha: RGBA into `out`.
void hsv(int hue, int saturation, int value, int alpha, uint8_t* out) {
    const double h = hue / 255.0, s = saturation / 255.0, v = value / 255.0;
    double r = v, g = v, b = v;
    if (s > 0) {
        const double sector = (h - std::floor(h)) * 6.0;
        const double f = sector - std::floor(sector);
        const double p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
        switch (int(sector)) {
            case 0: r = v, g = t, b = p; break;
            case 1: r = q, g = v, b = p; break;
            case 2: r = p, g = v, b = t; break;
            case 3: r = p, g = q, b = v; break;
            case 4: r = t, g = p, b = v; break;
            default: r = v, g = p, b = q; break;
        }
    }
    out[0] = uint8_t(r * 255 + 0.5);
    out[1] = uint8_t(g * 255 + 0.5);
    out[2] = uint8_t(b * 255 + 0.5);
    out[3] = uint8_t(std::clamp(alpha, 0, 255));
}

} // namespace

std::shared_ptr<const JPFrame> JPHsvIndicator::picture(const Ranges& c, bool enabled, int width, int height) {
    auto f = std::make_shared<JPFrame>();
    f->width = width;
    f->height = height;
    f->rgba.assign(size_t(width) * size_t(height) * 4, 0);
    const int diameter = std::min(width, height);
    const int unit = diameter / 5;
    const double radius = diameter / 2.0;
    const bool monochrome = c.minSaturation == 0 && c.maxSaturation == 255;
    auto at = [&](int x, int y) { return f->rgba.data() + (size_t(y) * size_t(width) + size_t(x)) * 4; };

    // The wheel.
    for (int y = 0; y < diameter; ++y)
        for (int x = 0; x < diameter; ++x) {
            const double dx = x - radius, dy = y - radius;
            const double r = std::sqrt(dx * dx + dy * dy);
            if (r >= radius) continue;
            const double saturation = 255.0 * r / radius;
            const double hue = 255.0 * std::fmod(1.0 + std::atan2(-dy, dx) / M_PI / 2.0, 1.0);
            const double alpha = std::min(1.0, radius - r);
            const double dHue = r / 64, dSat = radius / 255;
            const double inHue = c.minHue <= c.maxHue ? std::min(hue + 1 - c.minHue, c.maxHue + 1 - hue)
                                                      : std::max(hue + 1 - c.minHue, c.maxHue + 1 - hue);
            const double included = monochrome ? 1.0
                : std::max(0.0, std::min(1.0, std::min(dHue * inHue, dSat * std::min(saturation + 1 - c.minSaturation,
                                                                                     c.maxSaturation + 1 - saturation))));
            const double value = (included * (c.maxValue - c.minValue) + c.minValue) / 255;
            hsv(int(hue), enabled ? int(saturation) : 0, int(255 * value), int(255 * alpha), at(x, y));
        }

    // The value bar.
    const int hue = c.minHue <= c.maxHue ? (c.minHue + c.maxHue) / 2 : ((255 + c.minHue + c.maxHue) / 2) & 0xFF;
    const int x0 = diameter + unit, x1 = std::min(diameter + unit * 2, width);
    for (int y = 0; y < diameter; ++y) {
        const int value = 255 - 255 * y / diameter;
        const double dVal = diameter / 255.0;
        const double included = std::max(0.0, std::min(1.0, dVal * std::min(value - c.minValue, c.maxValue - value)));
        const double alpha = included * (1.0 - kExcludedRatio) + kExcludedRatio;
        for (int x = x0; x < x1; ++x) {
            const double r = double(x - x0) / (x1 - x0);
            const int saturation = monochrome ? 0
                : enabled && included > 0 ? int(r * (c.maxSaturation - c.minSaturation) + c.minSaturation) : 0;
            hsv(hue, saturation, value, int(255 * alpha), at(x, y));
        }
    }
    return f;
}

} // inline namespace jf
