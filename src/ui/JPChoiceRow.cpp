// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPChoiceRow.h"

#include <j/core/JStyle.h>
#include <j/core/JTextHelper.h>

#include <algorithm>
#include <cmath>

inline namespace jf {

JPChoiceRow::JPChoiceRow(JSceneGraph& graph, const std::vector<std::string>& labels, int chosen, int perRow)
    : JContainer(graph, 0.f, JStyle::current().buttonHeight) {
    const JStyle& st = JStyle::current();
    setGap(st.spacing)->setAlignItems(perRow > 0 ? JAlignItems::Start : JAlignItems::Center);
    setDirection(perRow > 0 ? JFlexDirection::Column : JFlexDirection::JRow);
    // In rows of `perRow`: every button the widest label's width, so they line up.
    float widest = 0;
    if (perRow > 0)
        for (const std::string& l : labels) widest = std::max(widest, JTextHelper::measureWidth(l));
    const float cell = std::ceil(widest) + 2 * st.itemPadding;
    JContainer* line = this;
    for (size_t i = 0; i < labels.size(); ++i) {
        if (perRow > 0 && i % size_t(perRow) == 0) {
            line = add(std::make_unique<JContainer>(graph, 0.f, st.buttonHeight));
            line->setDirection(JFlexDirection::JRow)->setGap(st.spacing)->setAlignItems(JAlignItems::Center);
            line->setFixedSize(perRow * cell + (perRow - 1) * st.spacing, st.buttonHeight);
        }
        // Zero design width: the button's own minimum fits its label.
        JToggleButton* b = line->add(std::make_unique<JToggleButton>(graph, labels[i], 0.f));
        if (perRow > 0) b->setFixedSize(cell, st.buttonHeight);
        b->onToggled.connect([this, i](bool) { if (!m_updating) choose(int(i)); });
        m_buttons.push_back(b);
    }
    if (perRow > 0) {
        const size_t rows = (labels.size() + size_t(perRow) - 1) / size_t(perRow);
        setFixedSize(perRow * cell + (perRow - 1) * st.spacing, rows * st.buttonHeight + (rows - 1) * st.spacing);
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

void JPChoiceRow::setChoicesEnabled(bool enabled) {
    for (JToggleButton* b : m_buttons) b->setEnabled(enabled);
}

} // inline namespace jf
