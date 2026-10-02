// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// Straightening a camera's picture: with a barrel lens bending about a point
// off the middle, and the camera turned and a little out of square, the
// straightened picture shows the machine square at one scale (a millimetre is
// as many pixels along x as along y, and the machine's X lies along the
// picture's x), keeps the middle where it is, and maps pixels both ways. Shown
// cropped, every part of it has picture behind it; shown whole, all of the
// picture is in it and its corners are bare.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "camera/JPStraightener.h"

#include <cmath>

using namespace jf;

namespace {

JPCameraCalibration bent() {
    JPCameraCalibration c;
    c.valid = true;
    c.pxPerMm = { -25.9, 0.45, 0.40, 26.1 };   // turned about a degree, not quite square
    c.lensK1 = -0.1;
    c.lensCentreX = 670;
    c.lensCentreY = 376;
    c.width = 1280;
    c.height = 720;
    return c;
}

// Where the straightened picture shows a machine point (dx, dy) mm from what the camera looks at.
void straightOf(const JPCameraCalibration& c, const JPStraightener& s, double dx, double dy, double& x, double& y) {
    double px, py;
    const bool seen = c.pixelFor(100 + dx, 50 + dy, 100, 50, px, py);
    assert(seen);
    s.toStraight(px, py, x, y);
}

} // namespace

int main() {
    const JPCameraCalibration c = bent();
    const auto cropped = JPStraightener::make(c, false, 0.0);
    const auto whole = JPStraightener::make(c, false, 1.0);
    assert(cropped && whole && cropped->width() == 1280 && cropped->height() == 720);

    // Square, at one scale, and the middle in the middle.
    double x0, y0, xx, xy, yx, yy;
    straightOf(c, *cropped, 0, 0, x0, y0);
    assert(std::abs(x0 - 640) < 1e-6 && std::abs(y0 - 360) < 1e-6);
    straightOf(c, *cropped, 5, 0, xx, xy);
    straightOf(c, *cropped, 0, 5, yx, yy);
    assert(std::abs(xy - y0) < 1e-6 && std::abs(yx - x0) < 1e-6);                 // X along x, Y along y
    assert(std::abs(std::abs(xx - x0) - std::abs(yy - y0)) < 1e-6);               // one scale both ways
    assert(xx > x0 && yy < y0);   // looking down, as from above: +X to the right, +Y up the picture

    // Both ways.
    double rx, ry, sx, sy;
    assert(cropped->toRaw(900.5, 140.25, rx, ry));
    cropped->toStraight(rx, ry, sx, sy);
    assert(std::abs(sx - 900.5) < 1e-6 && std::abs(sy - 140.25) < 1e-6);

    // Cropped: every node of the grid has picture behind it. Whole: the
    // corners are bare, and every edge pixel of the picture is inside it.
    for (const auto& n : cropped->grid()) assert(n.seen);
    const auto& g = whole->grid();
    const int cols = whole->columns();
    assert(!g.front().seen && !g[size_t(cols)].seen && !g.back().seen);
    for (int px = 0; px < 1280; px += 64)
        for (const int py : { 0, 719 }) {
            whole->toStraight(px, py, sx, sy);
            assert(sx >= 0 && sy >= 0 && sx <= 1279 && sy <= 719);
        }
    // Between them, between: more is seen at 1 than at 0.
    // (A mark 5 mm out is furthest from the middle cropped, nearest shown whole.)
    const auto half = JPStraightener::make(c, false, 0.5);
    double a, mid, b, ignored;
    straightOf(c, *cropped, 5, 0, a, ignored);
    straightOf(c, *half, 5, 0, mid, ignored);
    straightOf(c, *whole, 5, 0, b, ignored);
    assert(std::abs(b - 640) < std::abs(mid - 640) && std::abs(mid - 640) < std::abs(a - 640));

    // Not calibrated: nothing.
    assert(!JPStraightener::make(JPCameraCalibration(), false, 0));
    return 0;
}
