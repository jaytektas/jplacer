// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPFootprintDraw.h"

#include <cmath>
#include <vector>

inline namespace jf {

namespace {

constexpr double kPi = 3.14159265358979323846;

} // namespace

void JPFootprintDraw::draw(JVectorCanvas& vg, const JPFootprint& f, const Place& place, float line, const Colours& colours) {
    // The body, outlined.
    if (f.bodyWidth > 0 && f.bodyLength > 0) {
        const double hx = f.bodyWidth / 2, hy = f.bodyLength / 2;
        const double corners[5][2] = { { -hx, -hy }, { hx, -hy }, { hx, hy }, { -hx, hy }, { -hx, -hy } };
        std::vector<JVectorCanvas::JVec2> pts;
        for (const auto& c : corners) {
            float x, y;
            if (place(c[0], c[1], x, y)) pts.push_back({ x, y });
        }
        if (pts.size() == 5) vg.strokePolyline(pts, line, JPaint::solid(colours.body));
    }
    // Each pad, as the rectangle it is, turned.
    for (const JPPad& p : f.pads) {
        const double a = p.rotationDeg * kPi / 180, c = std::cos(a), s = std::sin(a);
        const double hw = p.width / 2, hh = p.height / 2;
        const double local[4][2] = { { -hw, -hh }, { hw, -hh }, { hw, hh }, { -hw, hh } };
        std::vector<JVectorCanvas::JVec2> pts;
        for (const auto& l : local) {
            float x, y;
            if (place(p.x + l[0] * c - l[1] * s, p.y + l[0] * s + l[1] * c, x, y)) pts.push_back({ x, y });
        }
        if (pts.size() == 4) vg.fillConvex(pts, JPaint::solid(colours.pad));
    }
    // Pin 1's dot, a quarter of the pad's smaller side across.
    if (const JPPad* p1 = f.pin1Pad()) {
        float x, y, ex, ey;
        const double r = std::min(p1->width, p1->height) / 4;
        if (place(p1->x, p1->y, x, y) && place(p1->x + r, p1->y, ex, ey))
            vg.fillCircle(x, y, std::max(line, std::hypot(ex - x, ey - y)), JPaint::solid(colours.pin1));
    }
}

} // inline namespace jf
