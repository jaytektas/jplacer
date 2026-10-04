// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPIconButton.h"

#include "JPOpenPnpIcons.h"

#include <j/core/JStyle.h>

#include <algorithm>

inline namespace jf {

namespace {

// The glyph's share of the button, and the corner rounding's.
constexpr float kGlyphShare  = 0.8f;
// The hint of where a click leads: inset from the corner, a triangle so wide,
// dots so big (shares of the button's side).
constexpr float kHintInset = 0.07f, kHintSize = 0.24f, kHintDot = 0.045f;
constexpr float kRoundShare  = 0.2f;
// An OpenPnP icon is drawn at this many times its size on screen.
constexpr float kIconOversample = 2.f;

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

JPIconButton::JPIconButton(JSceneGraph& graph, const std::string& name, const std::string& openPnpIcon,
                           const std::string& tooltip)
    : JPIconButton(graph, name, Glyph(), tooltip) {
    m_icon = openPnpIcon;
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
                        : m_framed                            ? st.Surface1 : nullptr;   // flat at rest
    if (fill) vg.fillRoundedRect(b.x, b.y, b.width, b.height, s * kRoundShare, JPaint::solid(colour(fill)));
    if (m_framed) {
        // The edge inside the button's box, not centred on it (half of it would be outside, and cut off).
        const float w = st.borderWidth, h = w * 0.5f;
        vg.strokeRoundedRect(b.x + h, b.y + h, b.width - w, b.height - w, s * kRoundShare, w, JPaint::solid(colour(st.Border)));
    }
    const JColor ink = !enabled ? colour(st.MutedText)
                     : m_checked ? colour(st.HighlightedText) : colour(st.TextPrimary);
    if (m_glyph) m_glyph(vg, b.x + b.width * 0.5f, b.y + b.height * 0.5f, s * kGlyphShare, ink);
    if (!m_icon.empty())
        if (JPOpenPnpIcons* icons = JPOpenPnpIcons::instance()) {
            // Drawn at twice its size and shown smaller, so it stays sharp on a scaled screen.
            const float side = s * kGlyphShare;
            const TextureHandle tex = icons->texture(m_icon, int(side * kIconOversample), !enabled);
            if (tex != kNullTexture) {
                vg.flush(buf);   // the button's face first, the icon over it
                buf.pushImage(b.x + (b.width - side) * 0.5f, b.y + (b.height - side) * 0.5f, side, side, tex);
            }
        }
    // Where a click leads, in the bottom-right corner.
    const float edge = s * kHintInset, right = b.x + b.width - edge, bottom = b.y + b.height - edge;
    if (m_leads == Leads::Menu) {
        const float w = s * kHintSize;
        vg.beginPath();
        vg.moveTo(right - w, bottom - w * 0.5f);
        vg.lineTo(right, bottom - w * 0.5f);
        vg.lineTo(right - w * 0.5f, bottom);
        vg.fill(JPaint::solid(ink));
    } else if (m_leads == Leads::Elsewhere) {
        const float r = s * kHintDot, gap = r * 3;
        for (int i = 0; i < 3; ++i) vg.fillCircle(right - r - i * gap, bottom - r, r, JPaint::solid(ink));
    }
    vg.flush(buf);
}

} // inline namespace jf
