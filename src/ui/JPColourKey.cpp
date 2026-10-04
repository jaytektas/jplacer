// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPColourKey.h"

#include <j/core/JStyle.h>
#include <j/core/JTextHelper.h>

inline namespace jf {

namespace {
// The stroke is as long as three em dashes, as OpenPnP draws it.
constexpr const char* kStrokeText = "MMM";
}

JPColourKey::JPColourKey(JSceneGraph& graph, const uint8_t* colour, std::string text)
    : JWidget(graph, "JPColourKey"), m_colour(colour), m_text(std::move(text)) {
    const JStyle& st = JStyle::current();
    setSize(JTextHelper::measureWidth(kStrokeText) + st.spacing + JTextHelper::measureWidth(m_text), st.labelHeight);
}

void JPColourKey::populateRenderPrimitives(JPrimitiveBuffer& buf) {
    const JStyle& st = JStyle::current();
    const JRect b = m_graph.getLayoutConst(m_nodeId).boundingBox;
    const float stroke = JTextHelper::measureWidth(kStrokeText);
    buf.pushRectangle(b.x, b.y + b.height * 0.5f - st.borderWidth, stroke, 2 * st.borderWidth, m_colour);
    JTextHelper::pushText(buf, b.x + stroke + st.spacing, b.y + (b.height - JTextHelper::lineHeight()) * 0.5f, m_text,
                          Colors::LabelText);
}

} // inline namespace jf
