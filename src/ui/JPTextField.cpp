// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPTextField.h"

inline namespace jf {

JPTextField::JPTextField(JSceneGraph& graph) : JLineEdit(graph, "") {
    onReturnPressed.connect([this] { commit(); });
}

void JPTextField::setValue(const std::string& text) {
    m_value = text;
    if (this->text() != text) setText(text);
}

bool JPTextField::handleKeyEvent(const JKeyEvent& ke) {
    if (ke.pressed && ke.key == JKeyEvent::JKey::Escape && text() != m_value) {
        setText(m_value);
        selectAll();
        return true;
    }
    return JLineEdit::handleKeyEvent(ke);
}

void JPTextField::onFocusEvent(bool focused) {
    JLineEdit::onFocusEvent(focused);
    if (!focused) commit();
}

void JPTextField::commit() {
    if (text() == m_value) return;
    m_value = text();
    onCommitted.emit(m_value);
}

} // inline namespace jf
