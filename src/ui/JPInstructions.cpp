// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPInstructions.h"

#include "JPUiParts.h"

#include <j/core/JStyle.h>

inline namespace jf {

namespace {
// Lines of text the instructions have room for.
constexpr float kLines = 3;
}

JPInstructions::JPInstructions(JSceneGraph& graph) : JPGroupFrame(graph, "") {
    setAlignItems(JAlignItems::Stretch);
    const JStyle& st = JStyle::current();
    m_text = add(std::make_unique<JLabel>(graph, "", 0.f, st.labelHeight * kLines));
    m_text->setWordWrap(true);
    m_text->setVSizePolicy(JSizePolicyMode::Fixed);
    m_text->setSize(0.f, st.labelHeight * kLines);
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
