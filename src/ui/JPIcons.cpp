// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPIcons.h"

#include <cmath>

inline namespace jf {

namespace {

// A line's width, as a share of the icon's size.
constexpr float kLine = 0.08f;
constexpr float kPi   = 3.14159265f;

// A camera's viewfinder: four corners of a square `half` from the centre.
void viewfinder(JVectorCanvas& vg, float cx, float cy, float half, float line, const JColor& ink) {
    const float arm = half * 0.45f;
    for (const float sx : { -1.f, 1.f })
        for (const float sy : { -1.f, 1.f }) {
            const float x = cx + sx * half, y = cy + sy * half;
            vg.strokePolyline({ { x - sx * arm, y }, { x, y }, { x, y - sy * arm } }, line, JPaint::solid(ink));
        }
}

// A nozzle, tip down, `h` tall: a wide collar, a body narrowing to the tip.
void nozzle(JVectorCanvas& vg, float cx, float top, float h, const JColor& ink) {
    const float w = h * 0.9f;
    vg.fillRect(cx - w * 0.5f, top, w, h * 0.22f, JPaint::solid(ink));
    vg.fillConvex({ { cx - w * 0.32f, top + h * 0.22f }, { cx + w * 0.32f, top + h * 0.22f },
                    { cx + w * 0.1f, top + h }, { cx - w * 0.1f, top + h } },
                  JPaint::solid(ink));
}

// An arrow pointing right, from `x0` to `x1` at height `y`.
void arrow(JVectorCanvas& vg, float x0, float x1, float y, float line, const JColor& ink) {
    const float head = (x1 - x0) * 0.45f;
    vg.drawLine(x0, y, x1 - line, y, line, JPaint::solid(ink));
    vg.fillConvex({ { x1, y }, { x1 - head, y - head * 0.7f }, { x1 - head, y + head * 0.7f } }, JPaint::solid(ink));
}

} // namespace

void JPIcons::eye(JVectorCanvas& vg, float cx, float cy, float size, const JColor& ink) {
    const float w = size * 0.45f, h = size * 0.3f, line = size * kLine;
    vg.beginPath();
    vg.moveTo(cx - w, cy);
    vg.quadTo(cx, cy - 2 * h, cx + w, cy);
    vg.quadTo(cx, cy + 2 * h, cx - w, cy);
    vg.close();
    vg.stroke(line, JPaint::solid(ink));
    vg.fillCircle(cx, cy, size * 0.14f, JPaint::solid(ink));
}

void JPIcons::save(JVectorCanvas& vg, float cx, float cy, float size, const JColor& ink) {
    const float half = size * 0.38f, line = size * kLine;
    vg.strokeRoundedRect(cx - half, cy - half, 2 * half, 2 * half, size * 0.06f, line, JPaint::solid(ink));
    // The shutter along the top, the label along the bottom.
    vg.fillRect(cx - half * 0.5f, cy - half, half, half * 0.6f, JPaint::solid(ink));
    vg.strokeRect(cx - half * 0.6f, cy + half * 0.15f, half * 1.2f, half * 0.85f, line * 0.8f, JPaint::solid(ink));
}

void JPIcons::target(JVectorCanvas& vg, float cx, float cy, float size, const JColor& ink) {
    const float r = size * 0.3f, line = size * kLine, out = size * 0.48f;
    vg.strokeCircle(cx, cy, r, line, JPaint::solid(ink));
    vg.fillCircle(cx, cy, size * 0.07f, JPaint::solid(ink));
    vg.drawLine(cx, cy - out, cx, cy - r * 0.6f, line, JPaint::solid(ink));
    vg.drawLine(cx, cy + r * 0.6f, cx, cy + out, line, JPaint::solid(ink));
    vg.drawLine(cx - out, cy, cx - r * 0.6f, cy, line, JPaint::solid(ink));
    vg.drawLine(cx + r * 0.6f, cy, cx + out, cy, line, JPaint::solid(ink));
}

void JPIcons::check(JVectorCanvas& vg, float cx, float cy, float size, const JColor& ink) {
    const float r = size * 0.42f, line = size * kLine;
    vg.strokeCircle(cx, cy, r, line, JPaint::solid(ink));
    vg.strokePolyline({ { cx - r * 0.5f, cy }, { cx - r * 0.12f, cy + r * 0.4f }, { cx + r * 0.52f, cy - r * 0.38f } },
                      line * 1.2f, JPaint::solid(ink));
}

void JPIcons::gear(JVectorCanvas& vg, float cx, float cy, float size, const JColor& ink) {
    // A ring, and eight teeth standing out from it.
    constexpr int kTeeth = 8;
    const float r = size * 0.27f, band = size * 0.14f, tip = size * 0.47f, halfTooth = size * 0.07f;
    vg.strokeCircle(cx, cy, r, band, JPaint::solid(ink));
    for (int i = 0; i < kTeeth; ++i) {
        const float a = 2 * kPi * float(i) / kTeeth;
        const float ux = std::cos(a), uy = std::sin(a), vx = -uy, vy = ux;
        const float in = r;
        vg.fillConvex({ { cx + ux * in + vx * halfTooth, cy + uy * in + vy * halfTooth },
                        { cx + ux * tip + vx * halfTooth, cy + uy * tip + vy * halfTooth },
                        { cx + ux * tip - vx * halfTooth, cy + uy * tip - vy * halfTooth },
                        { cx + ux * in - vx * halfTooth, cy + uy * in - vy * halfTooth } },
                      JPaint::solid(ink));
    }
}

namespace {

void boxed(JVectorCanvas& vg, float cx, float cy, float size, const JColor& ink, bool plus) {
    const float half = size * 0.36f, arm = size * 0.2f, line = size * kLine;
    vg.strokeRoundedRect(cx - half, cy - half, 2 * half, 2 * half, size * 0.06f, line, JPaint::solid(ink));
    vg.drawLine(cx - arm, cy, cx + arm, cy, line, JPaint::solid(ink));
    if (plus) vg.drawLine(cx, cy - arm, cx, cy + arm, line, JPaint::solid(ink));
}

} // namespace

void JPIcons::expandAll(JVectorCanvas& vg, float cx, float cy, float size, const JColor& ink) {
    boxed(vg, cx, cy, size, ink, true);
}

void JPIcons::collapseAll(JVectorCanvas& vg, float cx, float cy, float size, const JColor& ink) {
    boxed(vg, cx, cy, size, ink, false);
}

void JPIcons::captureCamera(JVectorCanvas& vg, float cx, float cy, float size, const JColor& ink) {
    const float line = size * kLine;
    viewfinder(vg, cx, cy, size * 0.42f, line, ink);
    vg.strokeCircle(cx, cy, size * 0.17f, line, JPaint::solid(ink));
}

void JPIcons::captureNozzle(JVectorCanvas& vg, float cx, float cy, float size, const JColor& ink) {
    const float line = size * kLine;
    nozzle(vg, cx, cy - size * 0.48f, size * 0.52f, ink);
    vg.strokeCircle(cx, cy + size * 0.24f, size * 0.2f, line, JPaint::solid(ink));
}

void JPIcons::moveCamera(JVectorCanvas& vg, float cx, float cy, float size, const JColor& ink) {
    // The viewfinder, and an arrow into its centre from the lower left.
    const float line = size * kLine;
    viewfinder(vg, cx, cy, size * 0.42f, line, ink);
    const float tip = size * 0.04f, tail = size * 0.3f, head = size * 0.16f;
    vg.drawLine(cx - tail, cy + tail, cx - tip, cy + tip, line, JPaint::solid(ink));
    vg.fillConvex({ { cx, cy }, { cx - head, cy }, { cx, cy + head } }, JPaint::solid(ink));
}

void JPIcons::moveNozzle(JVectorCanvas& vg, float cx, float cy, float size, const JColor& ink) {
    // The nozzle on the right, an arrow to it from the left.
    const float line = size * kLine;
    arrow(vg, cx - size * 0.48f, cx - size * 0.06f, cy, line, ink);
    nozzle(vg, cx + size * 0.24f, cy - size * 0.42f, size * 0.84f, ink);
}

} // inline namespace jf
