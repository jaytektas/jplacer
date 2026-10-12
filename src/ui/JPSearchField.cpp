// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPSearchField.h"

#include "JPTable.h"

#include <j/core/JStyle.h>
#include <j/core/JTextHelper.h>

#include <cctype>

inline namespace jf {

namespace {

// What follows a field's name completed: the colon a field's search is named by, and a space.
constexpr const char* kAfterName = ": ";

std::string folded(std::string s) {
    for (char& c : s) c = char(std::tolower(uint8_t(c)));
    return s;
}

} // namespace

JPSearchField::JPSearchField(JSceneGraph& graph) : JLineEdit(graph, "") {}

void JPSearchField::attach(JPTable& table) {
    m_table = &table;
    names = [this] { return m_table->columnNames(); };
    onTextChanged.connect([this](const std::string& t) { m_table->setFilter(t); });
    table.onViewChanged.connect([this] { updateCount(); });
    updateCount();
}

void JPSearchField::updateCount() {
    if (!m_count || !m_table) return;
    m_count->setText(m_table->searching() ? std::to_string(m_table->viewRowCount()) + " of " + std::to_string(m_table->unsearchedRowCount())
                                          : std::string());
}

std::string JPSearchField::completion() const {
    const std::string& t = text();
    if (t.empty() || t.find(':') != std::string::npos || !names || caret() != t.size()) return {};
    size_t start = 0;
    while (start < t.size() && std::isspace(uint8_t(t[start]))) ++start;
    const std::string typed = folded(t.substr(start));
    if (typed.empty()) return {};
    for (const std::string& n : names())
        if (n.size() > typed.size() && folded(n).compare(0, typed.size(), typed) == 0) return n.substr(typed.size()) + kAfterName;
    return {};
}

bool JPSearchField::handleKeyEvent(const JKeyEvent& ke) {
    using K = JKeyEvent::JKey;
    if (ke.pressed && !ke.ctrl && !ke.alt && !ke.shift && (ke.key == K::Tab || ke.key == K::Right)) {
        const std::string rest = completion();
        if (!rest.empty()) {
            // The name as the table has it, not as it was typed.
            const std::string t = text();
            size_t start = 0;
            while (start < t.size() && std::isspace(uint8_t(t[start]))) ++start;
            const std::string typed = folded(t.substr(start));
            for (const std::string& n : names())
                if (n.size() > typed.size() && folded(n).compare(0, typed.size(), typed) == 0) {
                    setText(n + kAfterName);
                    break;
                }
            return true;
        }
    }
    return JLineEdit::handleKeyEvent(ke);
}

void JPSearchField::populateRenderPrimitives(JPrimitiveBuffer& buf) {
    JLineEdit::populateRenderPrimitives(buf);
    if (!isFocused() || !JTextHelper::hasAtlas()) return;
    const std::string rest = completion();
    if (rest.empty()) return;
    // After the text as it is drawn (short enough not to scroll), greyed: a suggestion, not typed.
    const JRect b = m_graph.getLayoutConst(m_nodeId).boundingBox;
    const float pad = textPadding();
    const float x = b.x + pad + JTextHelper::measureWidth(displayText());
    const float room = b.x + b.width - pad - x;
    if (room <= 0) return;
    JTextHelper::pushText(buf, x, b.y + (b.height - JTextHelper::lineHeight()) * 0.5f, rest, Colors::MutedText, room);
}

} // inline namespace jf
