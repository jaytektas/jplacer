// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPMatView.h"

#include <j/core/JStyle.h>
#include <j/graphics/RenderPrimitive.h>

#include <algorithm>
#include <cmath>

inline namespace jf {

JPMatView::JPMatView(JSceneGraph& graph, JGpuHal* hal) : JWidget(graph, "JPMatView"), m_hal(hal) {}

JPMatView::~JPMatView() {
    if (m_hal && m_tex != kNullTexture) m_hal->releaseTexture(m_tex);
}

void JPMatView::setImage(std::shared_ptr<const JPFrame> image) {
    if (m_hal && m_tex != kNullTexture) m_hal->releaseTexture(m_tex);
    m_tex = kNullTexture;
    m_image = std::move(image);
    if (m_hal && m_image && m_image->width > 0 && m_image->height > 0)
        m_tex = m_hal->uploadTexture(m_image->rgba.data(), uint32_t(m_image->width), uint32_t(m_image->height));
    invalidate();
}

JRect JPMatView::picture() const {
    const JRect b = bounds();
    if (!m_image || m_image->width <= 0 || m_image->height <= 0 || b.width <= 0 || b.height <= 0) return { b.x, b.y, 0, 0 };
    // Both axes fitted, the shape kept.
    const float scale = std::min(b.width / float(m_image->width), b.height / float(m_image->height));
    const float w = float(m_image->width) * scale, h = float(m_image->height) * scale;
    return { b.x + (b.width - w) * 0.5f, b.y + (b.height - h) * 0.5f, w, h };
}

std::optional<std::pair<int, int>> JPMatView::pixelAt(float mx, float my) const {
    const JRect p = picture();
    if (p.width <= 0 || mx < p.x || my < p.y || mx >= p.x + p.width || my >= p.y + p.height) return std::nullopt;
    return std::pair { int((mx - p.x) / p.width * float(m_image->width)), int((my - p.y) / p.height * float(m_image->height)) };
}

void JPMatView::handleMouseMove(float mx, float my) {
    if (!onHover) return;
    if (const auto px = pixelAt(mx, my)) onHover(px->first, px->second);
}

void JPMatView::populateRenderPrimitives(JPrimitiveBuffer& buf) {
    const JRect b = bounds();
    buf.pushRectangle(b.x, b.y, b.width, b.height, Colors::DockContentBg, 0.f);
    if (m_tex == kNullTexture) return;
    const JRect p = picture();
    buf.pushImage(p.x, p.y, p.width, p.height, m_tex);
}

} // inline namespace jf
