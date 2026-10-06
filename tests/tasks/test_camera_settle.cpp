// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// Camera settling's measure of how much one picture differs from the one
// before, as OpenPnP's: its norms as percentages of full scale, in a centred
// circle; grey unless colour sensitive; and Motion, how far the picture moved.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "tasks/JPSettleCompare.h"

#include <cmath>
#include <cstdio>

using namespace jf;

namespace {

JPFrame flat(int w, int h, uint8_t r, uint8_t g, uint8_t b) {
    JPFrame f;
    f.width = w;
    f.height = h;
    for (int i = 0; i < w * h; ++i) f.rgba.insert(f.rgba.end(), { r, g, b, 255 });
    return f;
}

bool near(double a, double b, double tolerance = 1e-6) { return std::abs(a - b) < tolerance; }

double compared(const JPCameraConfig::Settle& s, const std::string& method, const JPFrame& a, const JPFrame& b) {
    JPSettleCompare c(s, method);
    const cv::Mat ma = c.prepare(a), mb = c.prepare(b);
    return c.difference(ma, mb);
}

} // namespace

int main() {
    JPCameraConfig::Settle s;
    const JPFrame a = flat(10, 10, 0, 0, 0), b = flat(10, 10, 51, 51, 51);   // 51 is a fifth of full scale
    assert(near(compared(s, "Euclidean", a, a), 0));
    assert(near(compared(s, "Maximum", a, b), 20));
    assert(near(compared(s, "Mean", a, b), 20));
    assert(near(compared(s, "Euclidean", a, b), 20));
    assert(near(compared(s, "Square", a, b), 4));

    // Only the centre counts with a mask: a change at a corner is not seen.
    JPFrame corner = a;
    corner.rgba[0] = corner.rgba[1] = corner.rgba[2] = 255;
    assert(compared(s, "Maximum", a, corner) == 100);
    JPCameraConfig::Settle masked = s;
    masked.maskCircle = 0.5;
    assert(compared(masked, "Maximum", a, corner) == 0);

    // Pictures of different sizes are as different as can be.
    assert(compared(s, "Euclidean", a, flat(5, 5, 0, 0, 0)) == 100);

    // A change of colour at the same brightness: unseen in grey, seen in colour.
    const JPFrame red = flat(10, 10, 120, 60, 60), green = flat(10, 10, 60, 90, 60);
    JPCameraConfig::Settle colour = s;
    colour.fullColor = true;
    assert(compared(colour, "Maximum", red, green) > 20 && compared(s, "Maximum", red, green) < compared(colour, "Maximum", red, green));

    // Gradients: a flat picture has none, whatever its level.
    JPCameraConfig::Settle edges = s;
    edges.gradients = true;
    assert(compared(edges, "Maximum", a, b) == 0);

    // Motion: a pattern moved 3 pixels is found 3 pixels away; unmoved, at once.
    JPFrame pattern = flat(100, 100, 0, 0, 0), moved = pattern;
    for (int y = 0; y < 100; ++y)
        for (int x = 0; x < 100; ++x) {
            const uint8_t v = uint8_t(((x / 7 + y / 5) % 2) * 200 + (x * 3 + y) % 40);
            const uint8_t m = uint8_t((((x + 3) / 7 + y / 5) % 2) * 200 + ((x + 3) * 3 + y) % 40);
            for (int c = 0; c < 3; ++c) {
                pattern.rgba[size_t(y * 100 + x) * 4 + size_t(c)] = v;
                moved.rgba[size_t(y * 100 + x) * 4 + size_t(c)] = m;
            }
        }
    assert(compared(s, "Motion", pattern, pattern) < 1);
    const double motion = compared(s, "Motion", pattern, moved);
    assert(motion >= 3 && motion < 4);

    // Contrast enhanced: a faint picture stretched to the full range, so a faint change counts for more.
    JPFrame faint = flat(10, 10, 0, 0, 0), changed;
    for (int i = 0; i < 100; ++i)
        for (int c = 0; c < 3; ++c) faint.rgba[size_t(i) * 4 + size_t(c)] = uint8_t((i + i / 10) % 2 ? 110 : 100);
    changed = faint;
    changed.rgba[0] = changed.rgba[1] = changed.rgba[2] = 105;
    JPCameraConfig::Settle contrast = s;
    contrast.contrastEnhance = 1;
    assert(compared(contrast, "Maximum", faint, faint) == 0);
    assert(compared(contrast, "Maximum", faint, changed) > 5 * compared(s, "Maximum", faint, changed));

    std::puts("test_camera_settle: ok");
    return 0;
}
