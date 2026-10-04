// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerChoiceDialog.h"

#include <j/core/JButton.h>
#include <j/core/JStyle.h>
#include <j/core/JTextHelper.h>

#include <algorithm>

inline namespace jf {

JPlacerChoiceDialog::JPlacerChoiceDialog(const std::string& title, const std::string& question,
                                         std::vector<std::string> options, int cancelIndex,
                                         std::function<void(int)> onChosen, JGpuHal& hal, int sx, int sy,
                                         NativeWinHandleType parent)
    : JDialogWindow(title, kW, kH, hal, sx, sy, parent), m_onChosen(std::move(onChosen)) {
    setResizable(true, kW / 2, kH / 2);
    m_question = std::make_unique<JLabel>(graph(), question, 0.f);
    m_question->setWordWrap(true);
    add(m_question.get());
    m_buttons = std::make_unique<JDialogButtonBox>(graph());
    for (size_t i = 0; i < options.size(); ++i) {
        const int index = int(i);
        const float w = JTextHelper::measureWidth(options[i]) + 4 * JStyle::current().spacing;
        if (index == cancelIndex) {
            m_buttons->addButton(options[i], JDialogButtonBox::Role::Reject, std::max(w, 84.f));
        } else {
            m_buttons->addButton(options[i], JDialogButtonBox::Role::Action, std::max(w, 84.f))
                ->onClicked.connect([this, index] { choose(index); });
        }
    }
    m_buttons->onReject.connect([this, cancelIndex] { choose(cancelIndex); });
    add(m_buttons.get());
}

JPlacerChoiceDialog::~JPlacerChoiceDialog() {
    if (!m_answered && m_onChosen) m_onChosen(-1);
}

void JPlacerChoiceDialog::choose(int index) {
    if (m_answered) return;
    m_answered = true;
    close();
    if (m_onChosen) m_onChosen(index);
}

void JPlacerChoiceDialog::layout(float w, float h) {
    const JStyle& st = JStyle::current();
    const float pad = st.spacing * 2, cw = w - 2 * pad;
    const float buttonsY = h - pad - st.buttonHeight;
    m_question->setBounds({ pad, contentTop(), cw, std::max(0.f, buttonsY - st.spacing * 2 - contentTop()) });
    m_buttons->setBounds({ pad, buttonsY, cw, st.buttonHeight });
}

} // inline namespace jf
