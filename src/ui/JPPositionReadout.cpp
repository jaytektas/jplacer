// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPPositionReadout.h"

#include "JPUiParts.h"

#include <j/core/JStyle.h>
#include <j/core/JTextHelper.h>

#include <algorithm>

inline namespace jf {

namespace {

constexpr const char* kRelative = "Relative  ";
// The widest it can read: relative, every coordinate at its longest.
constexpr const char* kWidest = "Relative  X -0000.000   Y -0000.000   Z -0000.000   C -0000.000";

} // namespace

JPPositionReadout::JPPositionReadout(JSceneGraph& graph, Source source)
    : JControl(graph, "Position"), m_source(std::move(source)) {
    onClicked.connect([this] { toggle(); });
    setTooltip("Where the tool chosen in Jog is. Click to measure from here; click again to go back.");
}

float JPPositionReadout::widthNeeded() {
    return JTextHelper::measureWidth(kWidest) + 2 * JStyle::current().spacing;
}

void JPPositionReadout::toggle() {
    m_relative = !m_relative;
    m_zero.clear();
    if (m_relative)
        for (const auto& [name, value] : m_source()) m_zero[name] = value;
    invalidate();
}

std::string JPPositionReadout::text() const {
    std::string coordinates;
    for (const auto& [name, value] : m_source()) {
        const auto z = m_zero.find(name);
        coordinates += (coordinates.empty() ? "" : "   ") + name + " "
                     + JPUiParts::coordinate(value - (z == m_zero.end() ? 0.0 : z->second));
    }
    return (m_relative ? kRelative : "") + coordinates;
}

void JPPositionReadout::populateRenderPrimitives(JPrimitiveBuffer& buf) {
    const auto& b = m_graph.getLayoutConst(m_nodeId).boundingBox;
    if (!JTextHelper::hasAtlas() || b.width <= 0) return;
    const JStyle& st = JStyle::current();
    const uint8_t* ink = m_relative ? st.Accent : st.Success;
    const std::string t = text();
    // To the right, where the status bar ends.
    const float x = std::max(b.x + st.spacing, b.x + b.width - st.spacing - JTextHelper::measureWidth(t));
    JTextHelper::pushText(buf, x, b.y + (b.height - JTextHelper::lineHeight()) * 0.5f, t, ink,
                          b.width - 2 * st.spacing);
}

} // inline namespace jf
