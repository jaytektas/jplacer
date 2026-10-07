// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPImageBox.h"

#include <j/core/JStyle.h>
#include <j/graphics/RenderPrimitive.h>

#include <algorithm>

inline namespace jf {

JPImageBox::JPImageBox(JSceneGraph& graph, JGpuHal* hal) : JWidget(graph, "JPImageBox"), m_hal(hal) {}

JPImageBox::~JPImageBox() {
    if (m_hal && m_tex != kNullTexture) m_hal->releaseTexture(m_tex);
}

void JPImageBox::setImage(std::shared_ptr<const JPFrame> image) {
    if (image == m_image) return;
    if (m_hal && m_tex != kNullTexture) m_hal->releaseTexture(m_tex);
    m_tex = kNullTexture;
    m_image = std::move(image);
    if (m_hal && m_image && m_image->width > 0 && m_image->height > 0)
        m_tex = m_hal->uploadTexture(m_image->rgba.data(), uint32_t(m_image->width), uint32_t(m_image->height));
    invalidate();
}

void JPImageBox::setFramed(bool framed) {
    if (framed == m_framed) return;
    m_framed = framed;
    invalidate();
}

void JPImageBox::populateRenderPrimitives(JPrimitiveBuffer& buf) {
    const JRect b = bounds();
    const JStyle& st = JStyle::current();
    if (m_framed) buf.pushRectangle(b.x, b.y, b.width, b.height, Colors::DockContentBg, 0.f, st.borderWidth, Colors::Border);
    if (m_tex != kNullTexture && m_image) {
        const float inset = m_framed ? st.borderWidth : 0.f;
        const float roomW = std::max(0.f, b.width - 2 * inset), roomH = std::max(0.f, b.height - 2 * inset);
        const float scale = std::min(roomW / float(m_image->width), roomH / float(m_image->height));
        const float w = float(m_image->width) * scale, h = float(m_image->height) * scale;
        buf.pushImage(b.x + (b.width - w) * 0.5f, b.y + (b.height - h) * 0.5f, w, h, m_tex);
    }
}

} // inline namespace jf
