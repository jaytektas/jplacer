// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <j/core/JLabel.h>
#include <j/core/JLineEdit.h>

#include <functional>
#include <string>
#include <vector>

inline namespace jf {

class JPTable;

// A table's search line (JPTable::setFilter) that completes the names of its fields: typing the start of
// one, before any colon, shows the rest of it greyed after the text ("lc" shows "sc: "); Tab, or Right at
// the end, takes it ("LCSC: "), and the search after it is in that field alone. Beside it, how many rows the
// search leaves of how many ("12 of 340"), while it searches.
class JPSearchField : public JLineEdit {
public:
    explicit JPSearchField(JSceneGraph& graph);

    // The label beside it its count is shown in (null: none).
    void setCountLabel(JLabel* count) { m_count = count; }
    // The table it searches: what is typed its filter, its columns' names completed, its rows counted.
    void attach(JPTable& table);

    // The names it completes (the table's columns', shown or not), asked as it is typed in.
    std::function<std::vector<std::string>()> names;

    // The name the text starts, the rest of it to show (empty: none).
    std::string completion() const;

    bool handleKeyEvent(const JKeyEvent& ke) override;
    void populateRenderPrimitives(JPrimitiveBuffer& buf) override;

private:
    void updateCount();

    JPTable* m_table = nullptr;
    JLabel*  m_count = nullptr;
};

} // inline namespace jf
