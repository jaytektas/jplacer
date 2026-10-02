// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPChoiceRow.h"

#include <j/core/JStyle.h>

inline namespace jf {

JPChoiceRow::JPChoiceRow(JSceneGraph& graph, const std::vector<std::string>& labels, int chosen)
    : JContainer(graph, 0.f, JStyle::current().buttonHeight) {
    setDirection(JFlexDirection::JRow)->setGap(JStyle::current().spacing)->setAlignItems(JAlignItems::Center);
    for (size_t i = 0; i < labels.size(); ++i) {
        // Zero design width: the button's own minimum fits its label.
        JToggleButton* b = add(std::make_unique<JToggleButton>(graph, labels[i], 0.f));
        b->onToggled.connect([this, i](bool) { if (!m_updating) choose(int(i)); });
        m_buttons.push_back(b);
    }
    choose(chosen);
}

void JPChoiceRow::choose(int index) {
    if (index < 0 || size_t(index) >= m_buttons.size()) return;
    // A click toggles the clicked button; the row decides what is down.
    m_updating = true;
    for (size_t i = 0; i < m_buttons.size(); ++i) m_buttons[i]->setToggled(int(i) == index);
    m_updating = false;
    if (index != m_chosen) {
        m_chosen = index;
        onChosen.emit(index);
    }
}

} // inline namespace jf
