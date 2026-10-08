// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "machine/JPCellConfig.h"

#include <j/core/UndoStack.h>

#include <functional>
#include <string>

inline namespace jf {

// Machine Setup's undo and redo: each change is the cell (and the node
// selected) before and after it. A change is recorded once it is made;
// undo and redo hand the state to go back (or forward) to to `restore`.
// Changes one after another to the same thing (a field typed into) are one
// step, until seal().
class JPSetupHistory {
public:
    // How many steps back are kept.
    static constexpr int kSteps = 200;

    struct State {
        JPCellConfig cell;
        std::string  selected;
    };

    explicit JPSetupHistory(std::function<void(const State&)> restore);

    // A change made: `what` says what it was ("Add Camera"); `key` names what
    // it changed, for joining a run of changes to the same thing (empty: never joined).
    void record(const std::string& what, const std::string& key, State before, State after);
    // The next change is a step of its own.
    void seal();

    bool canUndo() const { return m_stack.canUndo(); }
    bool canRedo() const { return m_stack.canRedo(); }
    // What undo (redo) would take back (do again).
    std::string undoText() const { return m_stack.undoText(); }
    std::string redoText() const { return m_stack.redoText(); }
    // Steps taken: counts each new step (not one joined to the last, an undo or a redo), to order them among
    // other histories.
    long long serial() const { return m_serial; }
    void undo();
    void redo();

    // The steps changed (one recorded, undone or done again).
    std::function<void()> onChanged;

private:
    JUndoStack                         m_stack;
    std::function<void(const State&)>  m_restore;
    bool                               m_recording = false;   // the change is made already: no restore
    std::string                        m_key;                 // what the last change changed
    int                                m_id = 0;              // its run's merge id
    long long                          m_serial = 0;
};

} // inline namespace jf
