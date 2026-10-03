// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPIconButton.h"

#include <j/core/JStyle.h>

#include <algorithm>

inline namespace jf {

namespace {

// The glyph's share of the button, and the corner rounding's.
constexpr float kGlyphShare  = 0.8f;
constexpr float kRoundShare  = 0.2f;

JColor colour(const uint8_t* c) { return rgb(c[0], c[1], c[2]); }

} // namespace

JPIconButton::JPIconButton(JSceneGraph& graph, const std::string& name, Glyph glyph, const std::string& tooltip)
    : JControl(graph, name), m_glyph(std::move(glyph)) {
    setTooltip(tooltip);
    const float s = size();
    auto& l = m_graph.getLayout(m_nodeId);
    l.boundingBox.width  = s;
    l.boundingBox.height = s;
    onClicked.connect([this] {
        if (!m_checkable) return;
        setChecked(!m_checked);
        onToggled.emit(m_checked);
    });
}

float JPIconButton::size() { return JStyle::current().tabBarSize; }

void JPIconButton::setChecked(bool on) {
    if (on == m_checked) return;
    m_checked = on;
    invalidate();
}

void JPIconButton::populateRenderPrimitives(JPrimitiveBuffer& buf) {
    const auto& b = m_graph.getLayoutConst(m_nodeId).boundingBox;
    const float s = std::min(b.width, b.height);
    if (s <= 0) return;
    const JStyle& st = JStyle::current();
    const JWidgetState state = getState();
    const bool enabled = state != JWidgetState::Disabled;
    JVectorCanvas vg;
    const uint8_t* fill = m_checked                           ? st.Accent
                        : state == JWidgetState::Pressed      ? st.Surface3
                        : state == JWidgetState::Hovered      ? st.Surface2
                                                              : nullptr;   // flat at rest
    if (fill) vg.fillRoundedRect(b.x, b.y, b.width, b.height, s * kRoundShare, JPaint::solid(colour(fill)));
    const JColor ink = !enabled ? colour(st.MutedText) : m_checked ? colour(st.HighlightedText) : colour(st.TextPrimary);
    m_glyph(vg, b.x + b.width * 0.5f, b.y + b.height * 0.5f, s * kGlyphShare, ink);
    vg.flush(buf);
}

} // inline namespace jf
