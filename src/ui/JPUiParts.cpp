// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPUiParts.h"

#include <j/core/JStyle.h>

#include <algorithm>
#include <cstdio>

inline namespace jf {

std::unique_ptr<JContainer> JPUiParts::row(JSceneGraph& graph) {
    const JStyle& st = JStyle::current();
    auto c = std::make_unique<JContainer>(graph, 0.f, std::max(st.buttonHeight, st.controlHeight));
    c->setDirection(JFlexDirection::JRow)->setGap(st.spacing)->setAlignItems(JAlignItems::Center);
    return c;
}

void JPUiParts::asPanel(JContainer& c) {
    const JStyle& st = JStyle::current();
    c.setDirection(JFlexDirection::Column)->setGap(2 * st.spacing)->setPadding(JEdges{ st.fieldPadding })
        ->setAlignItems(JAlignItems::Stretch);
}

std::unique_ptr<JButton> JPUiParts::button(JSceneGraph& graph, const std::string& label) {
    return std::make_unique<JButton>(graph, label, 0.f);
}

std::string JPUiParts::coordinate(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.3f", v);
    return buf;
}

} // inline namespace jf
