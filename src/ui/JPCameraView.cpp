// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPCameraView.h"

#include <j/core/JStyle.h>
#include <j/core/JTextHelper.h>
#include <j/core/MainThreadDispatcher.h>
#include <j/graphics/RenderPrimitive.h>
#include <j/graphics/VectorGraphics.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

inline namespace jf {

namespace {
// Each notch of the wheel zooms by this much: two notches double it.
const double kZoomPerNotch = std::sqrt(2.0);
} // namespace

JPCameraView::JPCameraView(JSceneGraph& graph, JGpuHal& hal)
    : JWidget(graph, "JPCameraView"), m_hal(hal) {}

JPCameraView::~JPCameraView() {
    *m_alive = false;
    if (m_unwatch) m_unwatch();
    dropTexture();
}

void JPCameraView::dropTexture() {
    if (m_tex != kNullTexture) m_hal.releaseTexture(m_tex);
    m_tex = kNullTexture;
}

void JPCameraView::setFeed(JPCameraFeed* feed) {
    if (m_unwatch) m_unwatch();
    m_unwatch = nullptr;
    m_feed = feed;
    m_have = 0;
    dropTexture();
    if (feed) {
        std::weak_ptr<bool> alive = m_alive;
        m_unwatch = feed->onFrame.connect([this, alive](uint64_t) {
            JMainThreadDispatcher::instance().post([this, alive] {
                if (const auto a = alive.lock(); a && *a) showLatest();
            });
        });
    }
    invalidate();
}

void JPCameraView::setMessage(const std::string& text) {
    m_message = text;
    invalidate();
}

void JPCameraView::showLatest() {
    // Posted frames can queue behind a busy main loop; take only the newest.
    if (!m_feed || !m_feed->latest(m_frame, m_have)) return;
    m_have = m_frame.sequence;
    dropTexture();
    m_tex = m_hal.uploadTexture(m_frame.rgba.data(), uint32_t(m_frame.width), uint32_t(m_frame.height));
    m_w = m_frame.width;
    m_h = m_frame.height;
    m_message.clear();
    invalidate();
}

void JPCameraView::populateRenderPrimitives(JPrimitiveBuffer& buf) {
    const JRect b = bounds();
    const JStyle& st = JStyle::current();
    buf.pushRectangle(b.x, b.y, b.width, b.height, Colors::DockContentBg, 0.f);
    if (m_tex == kNullTexture || m_w <= 0 || m_h <= 0) {
        if (!m_message.empty()) {
            const float tw = JTextHelper::measureWidth(m_message);
            JTextHelper::pushText(buf, b.x + std::max(0.f, (b.width - tw) * 0.5f),
                                  b.y + (b.height - JTextHelper::lineHeight()) * 0.5f, m_message,
                                  Colors::MutedText, b.width);
        }
        return;
    }
    // Fit the picture, shape kept, centred; zoomed about the centre, so the
    // crosshair stays where the camera is looking, and cut to the view.
    const float scale = std::min(b.width / float(m_w), b.height / float(m_h)) * float(m_zoom);
    const float w = float(m_w) * scale, h = float(m_h) * scale;
    const float x = b.x + (b.width - w) * 0.5f, y = b.y + (b.height - h) * 0.5f;
    buf.pushClip(b.x, b.y, b.width, b.height);
    // Straightened, the picture is drawn through the straightener's mesh: each
    // grid cell from where it is in the picture as taken (by the GPU where
    // there is one); cells the camera does not see are left out, bare.
    const bool straight = m_straight && m_straight->width() == m_w && m_straight->height() == m_h;
    if (straight) {
        const int cols = m_straight->columns(), rows = m_straight->rows();
        const auto& g = m_straight->grid();
        std::vector<JPrimitiveBuffer::JImageVertex> tris;
        tris.reserve(size_t(cols) * size_t(rows) * 6);
        auto node = [&](int c, int r) {
            const JPStraightener::Node& n = g[size_t(r) * size_t(cols + 1) + size_t(c)];
            return std::pair{ n, JPrimitiveBuffer::JImageVertex{ x + w * float(c) / float(cols), y + h * float(r) / float(rows), n.u, n.v } };
        };
        for (int r = 0; r < rows; ++r)
            for (int c = 0; c < cols; ++c) {
                const auto [n00, v00] = node(c, r);
                const auto [n10, v10] = node(c + 1, r);
                const auto [n01, v01] = node(c, r + 1);
                const auto [n11, v11] = node(c + 1, r + 1);
                if (!n00.seen || !n10.seen || !n01.seen || !n11.seen) continue;
                tris.insert(tris.end(), { v00, v10, v01, v10, v11, v01 });
            }
        buf.pushImageMesh(m_tex, std::move(tris));
    } else {
        buf.pushImage(x, y, w, h, m_tex);
    }
    m_picX = x;
    m_picY = y;
    m_picScale = scale;

    // What of the picture is on screen.
    const float vx0 = std::max(x, b.x), vy0 = std::max(y, b.y);
    const float vx1 = std::min(x + w, b.x + b.width), vy1 = std::min(y + h, b.y + b.height);

    // The crosshair: where the camera is looking.
    JVectorCanvas vg;
    const JColor c = rgb(Colors::Accent[0], Colors::Accent[1], Colors::Accent[2]);
    const float cx = x + w * 0.5f, cy = y + h * 0.5f, line = st.borderWidth;
    vg.drawLine(vx0, cy, vx1, cy, line, JPaint::solid(c));
    vg.drawLine(cx, vy0, cx, vy1, line, JPaint::solid(c));

    // What is on the machine there (the board's placements and fiducials).
    std::vector<JPViewMark> marks;
    if (m_marks) marks = m_marks();
    const JColor fidColour  = rgb(Colors::Warning[0], Colors::Warning[1], Colors::Warning[2]);
    const JColor partColour = rgb(Colors::Accent[0], Colors::Accent[1], Colors::Accent[2]);
    // Marks come in the picture-as-taken's pixels: shown where the picture shows them.
    struct Placed { float x, y; const JPViewMark* mark; };
    std::vector<Placed> placed;
    for (const JPViewMark& m : marks) {
        double sx, sy;
        if (!shown(m.x, m.y, sx, sy)) continue;
        placed.push_back({ x + float(sx) * scale, y + float(sy) * scale, &m });
    }
    for (const Placed& p : placed) {
        const float r = p.mark->radius > 0 ? float(p.mark->radius) * scale : st.spacing;
        vg.strokeCircle(p.x, p.y, r, line, JPaint::solid(p.mark->fiducial ? fidColour : partColour));
    }
    vg.flush(buf);
    for (const Placed& p : placed) {
        const float r = p.mark->radius > 0 ? float(p.mark->radius) * scale : st.spacing;
        JTextHelper::pushText(buf, p.x + r, p.y - r - JTextHelper::lineHeight(), p.mark->label,
                              p.mark->fiducial ? Colors::Warning : Colors::Accent);
    }

    // A picture left from before the camera was lost: say so over it, or it
    // would pass for a live one.
    const float lh = JTextHelper::lineHeight(), pad = st.spacing;
    if (!m_message.empty()) {
        buf.pushRectangle(vx0, vy0, vx1 - vx0, lh + 2 * pad, Colors::OverlayScrim, 0.f);
        JTextHelper::pushText(buf, vx0 + pad, vy0 + pad, m_message, Colors::Warning, vx1 - vx0 - 2 * pad);
    }
    // Zoomed: by how much, in the bottom corner.
    if (m_zoom > 1.0) {
        char text[32];
        std::snprintf(text, sizeof text, "%.0f%%", m_zoom * 100.0);
        const float tw = JTextHelper::measureWidth(text);
        const float zx = vx0, zy = vy1 - lh - 2 * pad;
        buf.pushRectangle(zx, zy, tw + 2 * pad, lh + 2 * pad, Colors::OverlayScrim, 0.f);
        JTextHelper::pushText(buf, zx + pad, zy + pad, text, Colors::ControlText);
    }
    buf.popClip();
}

bool JPCameraView::handleScroll(float, float, float wheel) {
    if (wheel == 0.f) return false;
    const double z = std::clamp(m_zoom * std::pow(kZoomPerNotch, double(wheel)), 1.0, kMostZoom);
    // Back near fitted is fitted, not 99.99% of it.
    m_zoom = z < 1.0 + 1e-6 ? 1.0 : z;
    invalidate();
    return true;
}

void JPCameraView::handleMousePress(float x, float y) {
    const JStyle& st = JStyle::current();
    const auto now = std::chrono::steady_clock::now();
    const bool twice = now - m_lastPress < std::chrono::milliseconds(int(st.doubleClickMs))
                    && std::abs(x - m_lastPressX) <= st.doubleClickSlop && std::abs(y - m_lastPressY) <= st.doubleClickSlop;
    m_lastPress = twice ? std::chrono::steady_clock::time_point() : now;   // a third press starts again
    m_lastPressX = x;
    m_lastPressY = y;
    if (!twice || m_picScale <= 0 || m_w <= 0 || !onPictureDoubleClicked) return;
    double px = (x - m_picX) / m_picScale, py = (y - m_picY) / m_picScale;
    if (px < 0 || py < 0 || px >= m_w || py >= m_h) return;
    // Straightened, the pixel clicked is back to the picture as taken.
    if (m_straight && m_straight->width() == m_w && m_straight->height() == m_h) {
        double rx, ry;
        if (!m_straight->toRaw(px, py, rx, ry)) return;
        px = rx;
        py = ry;
    }
    onPictureDoubleClicked(px, py);
}

bool JPCameraView::shown(double rawX, double rawY, double& x, double& y) const {
    if (m_straight && m_straight->width() == m_w && m_straight->height() == m_h)
        return m_straight->toStraight(rawX, rawY, x, y) && x >= 0 && y >= 0 && x < m_w && y < m_h;
    x = rawX;
    y = rawY;
    return true;
}

} // inline namespace jf
