// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPTableHistory.h"

#include "JPTable.h"

#include <j/core/Command.h>

inline namespace jf {

void JPTableHistory::record(JPTable& table, std::vector<Cell> cells, const std::string& text) {
    if (cells.empty()) return;
    JPTable* t = &table;
    m_recording = true;   // push() does the change; it is done already
    m_stack.push(new JFunctionCommand(
        text,
        [this, t, cells] {
            if (!m_recording && JPTable::alive(t)) t->putCells(cells, false);
            if (!m_recording && onChanged) onChanged();
        },
        [this, t, cells] {
            if (JPTable::alive(t)) t->putCells(cells, true);
            if (onChanged) onChanged();
        },
        -1));
    m_recording = false;
    ++m_serial;
    if (onChanged) onChanged();
}

} // inline namespace jf
