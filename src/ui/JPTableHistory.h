// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <j/core/UndoStack.h>

#include <functional>
#include <string>
#include <vector>

inline namespace jf {

class JPTable;

// The undo and redo of what is changed in tables (JPTable): each change one step, however many cells it changed
// (a value typed into all the rows chosen, a paste), named for it ("Change Rank of 12 Placements"); undone by
// putting each cell's value back, done again by setting them again, each found by its row's key and its
// column's key whatever the table shows now. The tables whose model has a history of its own (the library's)
// make no steps here. A step of a table no longer there does nothing.
class JPTableHistory {
public:
    static constexpr int kSteps = 200;   // steps back kept

    JPTableHistory() { m_stack.setUndoLimit(kSteps); }

    // One cell's change: its row's and its column's keys, its value before and after (as a cell's text).
    struct Cell {
        std::string row, column, before, after;
    };
    // A change made in `table`, kept as a step (it is made already).
    void record(JPTable& table, std::vector<Cell> cells, const std::string& text);

    bool canUndo() const { return m_stack.canUndo(); }
    bool canRedo() const { return m_stack.canRedo(); }
    std::string undoText() const { return m_stack.undoText(); }
    std::string redoText() const { return m_stack.redoText(); }
    void undo() { m_stack.undo(); }
    void redo() { m_stack.redo(); }
    // Steps taken (not undone or done again), to order them among other histories.
    long long serial() const { return m_serial; }
    // A step taken, undone or done again.
    std::function<void()> onChanged;

private:
    JUndoStack m_stack;
    long long  m_serial = 0;
    bool       m_recording = false;   // a step pushed is done already
};

} // inline namespace jf
