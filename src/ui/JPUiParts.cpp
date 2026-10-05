// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPUiParts.h"

#include "model/JPSystemUnits.h"

#include <j/core/JStyle.h>

#include <algorithm>
#include <cstdio>

inline namespace jf {

std::unique_ptr<JContainer> JPUiParts::row(JSceneGraph& graph) {
    const JStyle& st = JStyle::current();
    const float h = std::max(st.buttonHeight, st.controlHeight);
    auto c = std::make_unique<JContainer>(graph, 0.f, h);
    // Never shorter: a column short of room squeezed its rows flat, and the
    // buttons in them kept the height they were squeezed to, so a window made
    // small and then big again had lost them.
    c->setMinimumSize(0.f, h);
    c->setDirection(JFlexDirection::JRow)->setGap(st.spacing)->setAlignItems(JAlignItems::Center);
    return c;
}

void JPUiParts::asPanel(JContainer& c) {
    const JStyle& st = JStyle::current();
    c.setDirection(JFlexDirection::Column)->setGap(2 * st.spacing)->setPadding(JEdges{ st.fieldPadding })
        ->setAlignItems(JAlignItems::Stretch);
    // Short of room, lists and pictures give before rows of buttons and input lines.
    c.setShrinkStretchyFirst(true);
}

std::unique_ptr<JButton> JPUiParts::button(JSceneGraph& graph, const std::string& label) {
    return std::make_unique<JButton>(graph, label, 0.f);
}

std::string JPUiParts::coordinate(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.*f", JPSystemUnits::places(3), JPSystemUnits::shown(v));
    return buf;
}

std::string JPUiParts::angle(double degrees) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.3f", degrees);
    return buf;
}

} // inline namespace jf
