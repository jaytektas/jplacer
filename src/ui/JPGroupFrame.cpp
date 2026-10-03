// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPGroupFrame.h"

#include <j/core/JStyle.h>
#include <j/core/JTextHelper.h>
#include <j/graphics/VectorGraphics.h>

inline namespace jf {

namespace {

JColor colour(const uint8_t* c) { return rgb(c[0], c[1], c[2]); }

// The frame's top edge runs through the middle of the title.
float titleHeight() { return JTextHelper::lineHeight(); }

} // namespace

JPGroupFrame::JPGroupFrame(JSceneGraph& graph, std::string title) : JContainer(graph, 0.f, 0.f), m_title(std::move(title)) {
    const JStyle& st = JStyle::current();
    setDirection(JFlexDirection::Column)->setGap(st.spacing)->setAlignItems(JAlignItems::Start);
    setPadding(JEdges(st.itemPadding, titleHeight() + st.itemPadding, st.itemPadding, st.itemPadding));
}

float JPGroupFrame::extraHeight() {
    return titleHeight() + 2 * JStyle::current().itemPadding;
}

float JPGroupFrame::extraWidth() {
    return 2 * JStyle::current().itemPadding;
}

void JPGroupFrame::populateRenderPrimitives(JPrimitiveBuffer& buf) {
    const JStyle& st = JStyle::current();
    const JRect b = m_graph.getLayoutConst(m_nodeId).boundingBox;
    const float top = b.y + titleHeight() * 0.5f;
    const float titleWidth = JTextHelper::measureWidth(m_title);
    const float gapFrom = b.x + st.itemPadding - st.spacing, gapTo = b.x + st.itemPadding + titleWidth + st.spacing;
    // The frame, open where the title sits on it.
    JVectorCanvas vg;
    const JPaint line = JPaint::solid(colour(st.Border));
    const float w = st.borderWidth, right = b.x + b.width - w * 0.5f, bottom = b.y + b.height - w * 0.5f, left = b.x + w * 0.5f;
    vg.strokePolyline({ { gapFrom, top }, { left, top }, { left, bottom }, { right, bottom }, { right, top }, { gapTo, top } },
                      w, line);
    vg.flush(buf);
    JTextHelper::pushText(buf, b.x + st.itemPadding, b.y, m_title, st.LabelText);
    JContainer::populateRenderPrimitives(buf);
}

} // inline namespace jf
