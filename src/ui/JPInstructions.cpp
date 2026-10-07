// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPInstructions.h"

#include "JPUiParts.h"

#include <j/core/JStyle.h>

#include <algorithm>

inline namespace jf {

namespace {
// Lines of text the instructions have room for.
constexpr float kLines = 3;
// How wide its number is, in control heights.
constexpr float kNumberWidths = 4;
}

void JPInstructions::showNumber(const std::string& label, int value, int min, int max, std::function<void(int)> changed) {
    m_onNumber = nullptr;   // setting it up is not a change
    m_numberLabel->setText(label);
    m_number->setRange(min, max);
    m_number->setValue(value);
    m_onNumber = std::move(changed);
    m_numberRow->setVisible(true);
    m_numberRow->setFixedSize(0.f, JStyle::current().controlHeight);
    invalidate();
}

void JPInstructions::hideNumber() {
    m_onNumber = nullptr;
    m_numberRow->setVisible(false);
    m_numberRow->setFixedSize(0.f, 0.f);
    invalidate();
}

JPInstructions::JPInstructions(JSceneGraph& graph) : JPGroupFrame(graph, "") {
    setAlignItems(JAlignItems::Stretch);
    const JStyle& st = JStyle::current();
    m_text = add(std::make_unique<JLabel>(graph, "", 0.f, st.labelHeight * kLines));
    m_text->setWordWrap(true);
    m_text->setVSizePolicy(JSizePolicyMode::Fixed);
    m_text->setSize(0.f, st.labelHeight * kLines);
    // The number, on a row of its own over the buttons, there only while it is asked for.
    m_numberRow = add(JPUiParts::row(graph));
    // The field first, so a narrow place clips its label, not it.
    m_number = m_numberRow->add(std::make_unique<JSpinBox>(graph, 0, 1, st.controlHeight * kNumberWidths));
    m_numberLabel = m_numberRow->add(std::make_unique<JLabel>(graph, ""));
    m_number->onValueChanged.connect([this](int v) {
        if (auto f = m_onNumber) f(v);
    });
    hideNumber();
    auto row = JPUiParts::row(graph);
    row->add(std::make_unique<JContainer>(graph, 0.f, 0.f))->setHSizePolicy(JSizePolicyMode::Expanding, 1);
    m_cancel = row->add(JPUiParts::button(graph, "Cancel"));
    m_cancel->onClicked.connect([this] {
        if (auto f = m_onCancel) f();
    });
    m_proceed = row->add(JPUiParts::button(graph, "Next"));
    m_proceed->onClicked.connect([this] {
        if (auto f = m_onProceed) f();
    });
    add(std::move(row));
}

float JPInstructions::height() {
    const JStyle& st = JStyle::current();
    return st.labelHeight * kLines + st.buttonHeight + st.spacing * 2 + JPGroupFrame::extraHeight();
}

float JPInstructions::numberHeight() const {
    const JStyle& st = JStyle::current();
    return m_numberRow->isVisible() ? st.controlHeight + st.spacing : 0.f;
}

float JPInstructions::heightFor(float width) {
    const JStyle& st = JStyle::current();
    const float text = std::max(st.labelHeight * kLines, m_text->heightFor(std::max(0.f, width - JPGroupFrame::extraWidth())));
    m_text->setSize(0.f, text);
    return text + numberHeight() + st.buttonHeight + st.spacing * 2 + JPGroupFrame::extraHeight();
}

void JPInstructions::set(const std::string& title, const std::string& text, const std::string& proceedLabel,
                         std::function<void()> onCancel, std::function<void()> onProceed) {
    setTitle(title);
    m_text->setText(text);
    m_proceed->setLabel(proceedLabel);
    m_onCancel = std::move(onCancel);
    m_onProceed = std::move(onProceed);
    invalidate();
}

} // inline namespace jf
