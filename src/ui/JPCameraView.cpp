// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPCameraView.h"

#include <j/core/JStyle.h>
#include <j/core/JTextHelper.h>
#include <j/core/MainThreadDispatcher.h>
#include <j/graphics/RenderPrimitive.h>
#include <j/graphics/VectorGraphics.h>

#include <algorithm>

inline namespace jf {


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
    // Fit the picture, shape kept, centred.
    const float scale = std::min(b.width / float(m_w), b.height / float(m_h));
    const float w = float(m_w) * scale, h = float(m_h) * scale;
    const float x = b.x + (b.width - w) * 0.5f, y = b.y + (b.height - h) * 0.5f;
    buf.pushImage(x, y, w, h, m_tex);

    // The crosshair: where the camera is looking.
    JVectorCanvas vg;
    const JColor c = rgb(Colors::Accent[0], Colors::Accent[1], Colors::Accent[2]);
    const float cx = x + w * 0.5f, cy = y + h * 0.5f, line = st.borderWidth;
    vg.drawLine(x, cy, x + w, cy, line, JPaint::solid(c));
    vg.drawLine(cx, y, cx, y + h, line, JPaint::solid(c));

    // What is on the machine there (the board's placements and fiducials).
    std::vector<JPViewMark> marks;
    if (m_marks) marks = m_marks();
    const JColor fidColour  = rgb(Colors::Warning[0], Colors::Warning[1], Colors::Warning[2]);
    const JColor partColour = rgb(Colors::Accent[0], Colors::Accent[1], Colors::Accent[2]);
    for (const JPViewMark& m : marks) {
        const float mx = x + float(m.x) * scale, my = y + float(m.y) * scale;
        const float r = m.radius > 0 ? float(m.radius) * scale : st.spacing;
        vg.strokeCircle(mx, my, r, line, JPaint::solid(m.fiducial ? fidColour : partColour));
    }
    vg.flush(buf);
    for (const JPViewMark& m : marks) {
        const float mx = x + float(m.x) * scale, my = y + float(m.y) * scale;
        const float r = m.radius > 0 ? float(m.radius) * scale : st.spacing;
        JTextHelper::pushText(buf, mx + r, my - r - JTextHelper::lineHeight(), m.label,
                              m.fiducial ? Colors::Warning : Colors::Accent);
    }

    // A picture left from before the camera was lost: say so over it, or it
    // would pass for a live one.
    if (!m_message.empty()) {
        const float lh = JTextHelper::lineHeight(), pad = st.spacing;
        buf.pushRectangle(x, y, w, lh + 2 * pad, Colors::OverlayScrim, 0.f);
        JTextHelper::pushText(buf, x + pad, y + pad, m_message, Colors::Warning, w - 2 * pad);
    }
}

} // inline namespace jf
