// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPHistoryLineEdit.h"

inline namespace jf {

JPHistoryLineEdit::JPHistoryLineEdit(JSceneGraph& graph, const std::string& placeholder) : JLineEdit(graph, placeholder) {}

void JPHistoryLineEdit::remember(const std::string& line) {
    if (!line.empty() && (m_history.empty() || m_history.front() != line)) {
        m_history.push_front(line);
        if (m_history.size() > kMostKept) m_history.pop_back();
    }
    m_at = -1;
}

bool JPHistoryLineEdit::handleKeyEvent(const JKeyEvent& ke) {
    using K = JKeyEvent::JKey;
    if (ke.pressed && (ke.key == K::Up || ke.key == K::Down) && !m_history.empty()) {
        // Older going up, newer coming down; below the newest, the field empty again.
        if (ke.key == K::Up) m_at = std::min(m_at + 1, int(m_history.size()) - 1);
        else m_at = std::max(m_at - 1, -1);
        setText(m_at < 0 ? std::string() : m_history[size_t(m_at)]);
        return true;
    }
    return JLineEdit::handleKeyEvent(ke);
}

} // inline namespace jf
