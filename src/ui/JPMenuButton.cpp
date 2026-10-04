// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPMenuButton.h"

#include <j/core/JStyle.h>
#include <j/core/JTextHelper.h>
#include <j/graphics/VectorGraphics.h>

inline namespace jf {

namespace {

// Room after the label for the triangle (spaces, so the button's own width
// takes it in), and the triangle's width as a share of a line.
const std::string kRoom = "     ";
constexpr float kTriangleShare = 0.45f;

} // namespace

JPMenuButton::JPMenuButton(JSceneGraph& graph, const std::string& label) : JButton(graph, label + kRoom, 0.f) {}

void JPMenuButton::populateRenderPrimitives(JPrimitiveBuffer& buf) {
    JButton::populateRenderPrimitives(buf);
    const JRect b = bounds();
    const JStyle& st = JStyle::current();
    const float w = JTextHelper::lineHeight() * kTriangleShare;
    const float right = b.x + b.width - st.fieldPadding - st.spacing, mid = b.y + b.height * 0.5f;
    const uint8_t* c = getState() == JWidgetState::Disabled ? st.MutedText : st.ControlText;
    JVectorCanvas vg;
    vg.beginPath();
    vg.moveTo(right - w, mid - w * 0.25f);
    vg.lineTo(right, mid - w * 0.25f);
    vg.lineTo(right - w * 0.5f, mid + w * 0.3f);
    vg.fill(JPaint::solid(rgba(c[0], c[1], c[2], c[3])));
    vg.flush(buf);
}

} // inline namespace jf
