// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPStateIcon.h"

#include <j/core/JStyle.h>

#include <algorithm>

inline namespace jf {

namespace {

// The ring's proportions, as fractions of the icon's size (shape, not size:
// the icon is as big as the toolbar gives it).
constexpr float kRingRadius = 0.40f;
constexpr float kRingWidth  = 0.09f;
constexpr float kPi         = 3.14159265f;

JColor colour(const uint8_t* c) { return rgb(c[0], c[1], c[2]); }

} // namespace

JPStateIcon::JPStateIcon(JSceneGraph& graph, const char* name)
    : JControl(graph, name) {
    const float s = JStyle::current().buttonHeight;
    auto& l = m_graph.getLayout(m_nodeId);
    l.boundingBox.width  = s;
    l.boundingBox.height = s;
}

void JPStateIcon::setState(State s) {
    if (s == m_state) return;
    m_state = s;
    invalidate();
}

void JPStateIcon::populateRenderPrimitives(JPrimitiveBuffer& buf) {
    const auto& b = m_graph.getLayoutConst(m_nodeId).boundingBox;
    const float size = std::min(b.width, b.height);
    if (size <= 0) return;
    const float cx = b.x + b.width * 0.5f, cy = b.y + b.height * 0.5f;
    const JStyle& st = JStyle::current();

    JColor ring = colour(st.MutedText);
    if (m_state == State::Busy)  ring = colour(st.Warning);
    if (m_state == State::Good)  ring = colour(st.Success);
    if (m_state == State::Fault) ring = colour(st.Danger);
    const JColor ink = isEnabled() ? colour(st.TextPrimary) : colour(st.MutedText);

    JVectorCanvas vg;
    const float r = size * kRingRadius, w = size * kRingWidth;
    if (m_state == State::Busy)       vg.strokeArc(cx, cy, r, -kPi / 2, kPi, w, JPaint::solid(ring));       // three quarters
    else if (m_state == State::Fault) vg.strokeArc(cx, cy, r, kPi / 6, kPi / 6 + 5 * kPi / 3, w, JPaint::solid(ring)); // broken
    else                              vg.strokeCircle(cx, cy, r, w, JPaint::solid(ring));
    drawGlyph(vg, cx, cy, size, ink);
    vg.flush(buf);
}

} // inline namespace jf
