// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPCompositingPreview.h"

#include <j/core/JStyle.h>
#include <j/core/JTextHelper.h>
#include <j/graphics/VectorGraphics.h>

#include <algorithm>
#include <cmath>

inline namespace jf {

namespace {

// The margin round the drawing, in pixels (OpenPnP's 16 across both sides).
constexpr float kMarginPx = 8;
// How opaque the shades, fills and circles are (OpenPnP's alphas, of 255).
constexpr float kShadeNear = 180 / 255.f, kShadeFar = 64 / 255.f, kViewFill = 32 / 255.f, kFusedPads = 220 / 255.f;
constexpr float kMinCircle = 128 / 255.f, kMaxCircle = 64 / 255.f, kRoamingCircle = 128 / 255.f;
// A shade's colour: a quarter of the shot's (OpenPnP's colour / 4).
constexpr float kShadeShare = 0.25f;

JColor colour(const uint8_t c[4], float alpha = 1) { return rgba(c[0], c[1], c[2], uint8_t(std::lround(c[3] * alpha))); }

JColor shadeOf(const uint8_t c[4], float alpha) {
    return rgba(uint8_t(c[0] * kShadeShare), uint8_t(c[1] * kShadeShare), uint8_t(c[2] * kShadeShare), uint8_t(std::lround(255 * alpha)));
}

} // namespace

JPCompositingPreview::JPCompositingPreview(JSceneGraph& graph) : JWidget(graph, "JPCompositingPreview") {}

void JPCompositingPreview::setComposite(std::shared_ptr<const JPVisionComposite> composite, const JPFootprint& footprintMm,
                                        double cameraWidthMm, double cameraHeightMm, double roamingRadiusMm) {
    m_composite = std::move(composite);
    m_footprint = footprintMm;
    m_cameraWidth = cameraWidthMm;
    m_cameraHeight = cameraHeightMm;
    m_roaming = roamingRadiusMm;
    invalidate();
}

void JPCompositingPreview::clear() {
    m_composite.reset();
    invalidate();
}

void JPCompositingPreview::handleMousePress(float mx, float my) {
    if (!isPointInside(mx, my)) return;   // every widget of the form hears a press: only one here is its
    m_pressed = true;
    invalidate();
}

void JPCompositingPreview::handleMouseRelease(float, float) {
    m_pressed = false;
    invalidate();
}

void JPCompositingPreview::handleMouseMove(float mx, float my) {
    m_mouse = { mx, my };
    invalidate();
}

void JPCompositingPreview::setState(JWidgetState s) {
    if (s == JWidgetState::Normal) {
        m_mouse.reset();
        m_pressed = false;
    }
    JWidget::setState(s);
}

void JPCompositingPreview::populateRenderPrimitives(JPrimitiveBuffer& buf) {
    const JRect b = bounds();
    buf.pushRectangle(b.x, b.y, b.width, b.height, Colors::ChartBg);
    if (!m_composite) {
        const std::string text = "no data";
        const float tw = JTextHelper::measureWidth(text);
        JTextHelper::pushText(buf, b.x + (b.width - tw) / 2, b.y + b.height / 2, text, Colors::MutedText);
        return;
    }
    buf.pushClip(b.x, b.y, b.width, b.height);
    // The footprint's bounds, the camera's widest view round it: the scale.
    double x0 = -m_footprint.bodyWidth / 2, x1 = m_footprint.bodyWidth / 2, y0 = -m_footprint.bodyHeight / 2,
           y1 = m_footprint.bodyHeight / 2;
    const auto padOutlines = m_footprint.padsOutlines();
    for (const auto& o : padOutlines)
        for (const auto& p : o) {
            x0 = std::min(x0, p.x);
            x1 = std::max(x1, p.x);
            y0 = std::min(y0, p.y);
            y1 = std::max(y1, p.y);
        }
    const double cameraMaxRadius = std::min(m_cameraWidth, m_cameraHeight) / 2;
    const double scale = std::max(1e-9, std::min((b.width - 2 * kMarginPx) / (x1 - x0 + cameraMaxRadius * 2),
                                                 (b.height - 2 * kMarginPx) / (y1 - y0 + cameraMaxRadius * 2)));
    const float cx = b.x + b.width / 2, cy = b.y + b.height / 2;
    auto sx = [&](double x) { return float(cx + x * scale); };
    auto sy = [&](double y) { return float(cy - y * scale); };   // y up
    const float px = 1;   // one pixel, OpenPnP's 1/scale in the part's frame
    JVectorCanvas vg;

    // The package: body, then pads.
    {
        std::vector<JVectorCanvas::JVec2> body;
        for (const auto& p : m_footprint.bodyOutline()) body.push_back({ sx(p.x), sy(p.y) });
        if (body.size() >= 3) vg.fillConvex(body, JPaint::solid(colour(Colors::Surface3)));
        for (const auto& o : padOutlines) {
            std::vector<JVectorCanvas::JVec2> pad;
            for (const auto& p : o) pad.push_back({ sx(p.x), sy(p.y) });
            if (pad.size() >= 3) vg.fillConvex(pad, JPaint::solid(colour(Colors::TextPrimary)));
        }
    }
    // Pressed: the pads as fused.
    if (m_pressed)
        for (const JPFootprint::Pad& pad : m_composite->rectifiedPads())
            vg.fillRect(sx(pad.x - pad.width / 2), sy(pad.y + pad.height / 2), float(pad.width * scale), float(pad.height * scale),
                        JPaint::solid(colour(Colors::TextPrimary, kFusedPads)));

    auto circle = [&](double x, double y, double r, float width, JColor c) {
        if (r > 0) vg.strokeCircle(sx(x), sy(y), float(r * scale), width, JPaint::solid(c));
    };
    auto drawShot = [&](const JPVisionComposite::Shot& shot, const uint8_t* c) {
        circle(shot.x, shot.y, shot.minMaskRadius, 2 * px, colour(c, kMinCircle));
        circle(shot.x, shot.y, shot.maxMaskRadius, 2 * px, colour(c, kMaxCircle));
        for (const JPVisionComposite::Corner* corner : shot.corners) {
            // Each corner as its two edges, as long as its least mask radius, drawn 4 pixels wide.
            const float stroke = 4 * px;
            const double s = stroke / scale;
            vg.drawLine(sx(corner->x + corner->xSign * s), sy(corner->y + corner->ySign * s),
                        sx(corner->x - corner->xSign * corner->minMaskRadius + corner->xSign * s / 2), sy(corner->y + corner->ySign * s),
                        stroke, JPaint::solid(colour(c)));
            vg.drawLine(sx(corner->x + corner->xSign * s), sy(corner->y),
                        sx(corner->x + corner->xSign * s), sy(corner->y - corner->ySign * corner->minMaskRadius + corner->ySign * s / 2),
                        stroke, JPaint::solid(colour(c)));
            if (m_pressed) circle(corner->x, corner->y, corner->maxMaskRadius, 0.5f * px, colour(c, kMaxCircle));
        }
    };
    // What the camera sees with a shot over it: shaded but for the shot's mask and the camera's view.
    auto drawCameraView = [&](double camX, double camY, const JPVisionComposite::Shot& shot, const uint8_t* c) {
        const float far = std::hypot(b.width, b.height);
        vg.fillRing(sx(shot.x), sy(shot.y), float(shot.maxMaskRadius * scale), far, 0, float(2 * M_PI), JPaint::solid(shadeOf(c, kShadeNear)));
        vg.fillCircle(sx(shot.x), sy(shot.y), float(shot.maxMaskRadius * scale), JPaint::solid(colour(Colors::TextPrimary, kViewFill)));
        circle(shot.x, shot.y, shot.minMaskRadius, px, colour(c));
        // Outside the camera's view, a lighter shade.
        const float vx0 = sx(camX - m_cameraWidth / 2), vx1 = sx(camX + m_cameraWidth / 2);
        const float vy0 = sy(camY + m_cameraHeight / 2), vy1 = sy(camY - m_cameraHeight / 2);
        const JPaint shade = JPaint::solid(shadeOf(c, kShadeFar));
        vg.fillRect(b.x, b.y, b.width, std::max(0.f, vy0 - b.y), shade);
        vg.fillRect(b.x, vy1, b.width, std::max(0.f, b.y + b.height - vy1), shade);
        vg.fillRect(b.x, vy0, std::max(0.f, vx0 - b.x), vy1 - vy0, shade);
        vg.fillRect(vx1, vy0, std::max(0.f, b.x + b.width - vx1), vy1 - vy0, shade);
        vg.strokeRect(vx0, vy0, vx1 - vx0, vy1 - vy0, px, JPaint::solid(colour(Colors::Border)));
        // The roaming radius round it, half the tolerance out, the tolerance wide.
        if (m_roaming > 0)
            circle(shot.x, shot.y, m_roaming + m_composite->tolerance() / 2,
                   std::max(px, float(m_composite->tolerance() * scale)), colour(Colors::Danger, kRoamingCircle));
    };

    const auto& shots = m_composite->shots();
    if (JPVisionComposite::isInvalid(m_composite->solution())) {
        for (const auto& shot : shots) {
            drawCameraView(0, 0, shot, Colors::Danger);
            drawShot(shot, Colors::Danger);
        }
        vg.drawLine(b.x, b.y + b.height, b.x + b.width, b.y, px, JPaint::solid(colour(Colors::Danger)));
    } else {
        // The shot under the mouse, the nearest whose mask holds it.
        const JPVisionComposite::Shot* over = nullptr;
        if (m_mouse) {
            const double mx = (m_mouse->first - cx) / scale, my = (cy - m_mouse->second) / scale;
            double best = INFINITY;
            for (const auto& shot : shots)
                if (const double d = std::hypot(mx - shot.x, my - shot.y); d < shot.maxMaskRadius && d < best) {
                    best = d;
                    over = &shot;
                }
        }
        if (over) {
            drawCameraView(over->x, over->y, *over, Colors::ChartBg);
            drawShot(*over, Colors::Warning);
        } else {
            // In reverse, the first on top.
            for (auto s = shots.rbegin(); s != shots.rend(); ++s) drawShot(*s, s->optional ? Colors::Accent : Colors::Danger);
        }
    }
    vg.flush(buf);
    buf.popClip();
}

} // inline namespace jf
