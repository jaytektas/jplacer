// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPTextBox.h"

#include <j/core/JStyle.h>
#include <j/core/JTextHelper.h>

inline namespace jf {

JPTextBox::JPTextBox(JSceneGraph& graph, const std::string& placeholder) : JTextArea(graph, placeholder) {}

void JPTextBox::setValue(const std::string& text) {
    m_value = text;
    if (this->text() != text) setText(text);
}

float JPTextBox::heightFor(int lines) {
    const JStyle& st = JStyle::current();
    return std::max(st.controlHeight, lines * JTextHelper::lineHeight() + 2 * st.fieldPadding);
}

bool JPTextBox::handleKeyEvent(const JKeyEvent& ke) {
    if (ke.pressed && ke.key == JKeyEvent::JKey::Escape && text() != m_value) {
        setText(m_value);
        return true;
    }
    return JTextArea::handleKeyEvent(ke);
}

void JPTextBox::onFocusEvent(bool focused) {
    JTextArea::onFocusEvent(focused);
    if (focused || text() == m_value) return;
    m_value = text();
    onCommitted.emit(m_value);
}

} // inline namespace jf
