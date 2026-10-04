// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPIcons.h"

#include <cmath>
#include <vector>

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

// An arrow pointing along (dx, dy), a unit direction, through the centre.
void pointing(JVectorCanvas& vg, float cx, float cy, float size, float dx, float dy, const JColor& ink) {
    const float line = size * kLine, half = size * 0.36f, head = size * 0.2f;
    const float tx = cx + dx * half, ty = cy + dy * half;
    vg.drawLine(cx - dx * half, cy - dy * half, tx - dx * head * 0.5f, ty - dy * head * 0.5f, line, JPaint::solid(ink));
    // The head: back from the tip, out to either side.
    const float bx = tx - dx * head, by = ty - dy * head, px = -dy * head * 0.75f, py = dx * head * 0.75f;
    vg.fillConvex({ { tx, ty }, { bx + px, by + py }, { bx - px, by - py } }, JPaint::solid(ink));
}

// Most of a circle, ending in an arrowhead going `clockwise` or not (y down).
void turning(JVectorCanvas& vg, float cx, float cy, float size, bool clockwise, const JColor& ink) {
    const float r = size * 0.3f, line = size * kLine, head = size * 0.16f;
    constexpr int kSegments = 24;
    // From the top round 300 degrees, the way it turns; the arrowhead at the end.
    const float start = -kPi / 2, sweep = (clockwise ? 1.f : -1.f) * kPi * 5 / 3;
    std::vector<JVectorCanvas::JVec2> arc;
    for (int i = 0; i <= kSegments; ++i) {
        const float a = start + sweep * float(i) / kSegments;
        arc.push_back({ cx + r * std::cos(a), cy + r * std::sin(a) });
    }
    vg.strokePolyline(arc, line, JPaint::solid(ink));
    const float a = start + sweep, s = clockwise ? 1.f : -1.f;
    const float ex = cx + r * std::cos(a), ey = cy + r * std::sin(a);
    const float tx = -std::sin(a) * s, ty = std::cos(a) * s;   // along the turn
    const float nx = std::cos(a), ny = std::sin(a);            // outward
    vg.fillConvex({ { ex + tx * head, ey + ty * head }, { ex + nx * head * 0.8f, ey + ny * head * 0.8f },
                    { ex - nx * head * 0.8f, ey - ny * head * 0.8f } },
                  JPaint::solid(ink));
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
    arrow(vg, cx - size * 0.48f, cx - size * 0.1f, cy, line, ink);
    nozzle(vg, cx + size * 0.2f, cy - size * 0.36f, size * 0.66f, ink);   // all of it within the icon
}

void JPIcons::arrowUp(JVectorCanvas& vg, float cx, float cy, float size, const JColor& ink) { pointing(vg, cx, cy, size, 0, -1, ink); }
void JPIcons::arrowDown(JVectorCanvas& vg, float cx, float cy, float size, const JColor& ink) { pointing(vg, cx, cy, size, 0, 1, ink); }
void JPIcons::arrowLeft(JVectorCanvas& vg, float cx, float cy, float size, const JColor& ink) { pointing(vg, cx, cy, size, -1, 0, ink); }
void JPIcons::arrowRight(JVectorCanvas& vg, float cx, float cy, float size, const JColor& ink) { pointing(vg, cx, cy, size, 1, 0, ink); }

void JPIcons::rotateAnticlockwise(JVectorCanvas& vg, float cx, float cy, float size, const JColor& ink) {
    turning(vg, cx, cy, size, false, ink);
}

void JPIcons::rotateClockwise(JVectorCanvas& vg, float cx, float cy, float size, const JColor& ink) {
    turning(vg, cx, cy, size, true, ink);
}

void JPIcons::home(JVectorCanvas& vg, float cx, float cy, float size, const JColor& ink) {
    // A roof and a body with a door.
    const float w = size * 0.36f, roof = size * 0.38f, body = size * 0.3f;
    vg.fillConvex({ { cx, cy - roof }, { cx + w * 1.2f, cy - size * 0.02f }, { cx - w * 1.2f, cy - size * 0.02f } }, JPaint::solid(ink));
    vg.fillRect(cx - w * 0.8f, cy - size * 0.02f, w * 1.6f, body, JPaint::solid(ink));
}

void JPIcons::nozzleTip(JVectorCanvas& vg, float cx, float cy, float size, const JColor& ink) {
    // The nozzle, then a gap, then its tip: a narrow tube ending in a point.
    nozzle(vg, cx, cy - size * 0.46f, size * 0.5f, ink);
    const float w = size * 0.12f, top = cy + size * 0.1f, bottom = cy + size * 0.46f;
    vg.fillConvex({ { cx - w, top }, { cx + w, top }, { cx + w * 0.45f, bottom }, { cx - w * 0.45f, bottom } },
                  JPaint::solid(ink));
}

void JPIcons::park(JVectorCanvas& vg, float cx, float cy, float size, const JColor& ink) {
    // The sign's edge, and a P drawn as a stem and a bowl: lines, so it scales.
    const float half = size * 0.4f, line = size * kLine;
    vg.strokeRoundedRect(cx - half, cy - half, 2 * half, 2 * half, size * 0.1f, line, JPaint::solid(ink));
    const float stroke = size * 0.11f, left = cx - size * 0.14f, top = cy - size * 0.24f, bottom = cy + size * 0.25f;
    const float bowlR = size * 0.12f, bowlMid = top + bowlR;
    vg.drawLine(left, top, left, bottom, stroke, JPaint::solid(ink));
    std::vector<JVectorCanvas::JVec2> bowl{ { left, top } };
    constexpr int kSegments = 12;
    for (int i = 0; i <= kSegments; ++i) {
        const float a = -kPi / 2 + kPi * float(i) / kSegments;
        bowl.push_back({ left + size * 0.1f + bowlR * std::cos(a), bowlMid + bowlR * std::sin(a) });
    }
    bowl.push_back({ left, top + 2 * bowlR });
    vg.strokePolyline(bowl, stroke, JPaint::solid(ink));
}

} // inline namespace jf
