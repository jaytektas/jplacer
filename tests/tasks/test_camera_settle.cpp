// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// Camera settling's measure of how much one picture differs from the one
// before: OpenPnP's norms, as percentages of full scale, in a centred circle.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "tasks/JPCameraLook.h"

#include <cmath>
#include <cstdio>

using namespace jf;

namespace {

JPGrayImage flat(int w, int h, float v) {
    JPGrayImage g;
    g.width = w;
    g.height = h;
    g.pixels.assign(size_t(w) * size_t(h), v);
    return g;
}

bool near(double a, double b) { return std::abs(a - b) < 1e-6; }

} // namespace

int main() {
    const JPGrayImage a = flat(10, 10, 0), b = flat(10, 10, 51);   // 51 is a fifth of full scale
    assert(near(JPCameraLook::difference(a, a, "Euclidean", 0), 0));
    assert(near(JPCameraLook::difference(a, b, "Maximum", 0), 20));
    assert(near(JPCameraLook::difference(a, b, "Mean", 0), 20));
    assert(near(JPCameraLook::difference(a, b, "Euclidean", 0), 20));
    assert(near(JPCameraLook::difference(a, b, "Square", 0), 4));

    // Only the centre counts with a mask: a change at a corner is not seen.
    JPGrayImage corner = a;
    corner.pixels[0] = 255;
    assert(JPCameraLook::difference(a, corner, "Maximum", 0) == 100);
    assert(JPCameraLook::difference(a, corner, "Maximum", 0.5) == 0);

    // Pictures of different sizes are as different as can be.
    assert(JPCameraLook::difference(a, flat(5, 5, 0), "Euclidean", 0) == 100);

    std::puts("test_camera_settle: ok");
    return 0;
}
