// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "model/JPConfiguration.h"

#include <j/core/UndoStack.h>

#include <functional>
#include <memory>
#include <string>

inline namespace jf {

// The library's undo and redo (its parts, packages, footprints and manufacturers): each step is the library
// before and after a change, named for what changed ("Delete Part R1", "Change Package SOT-23", "New Footprint
// …"). A change is noticed after it is made (note(): the library compared with how it was last noted), so every
// way the library is changed is a step, whatever made it.
class JPLibraryHistory {
public:
    static constexpr int kSteps = 200;   // steps back kept

    // `restored`: the library was put back (undone or done again): shown anew.
    JPLibraryHistory(JPConfiguration& config, std::function<void()> restored);

    // The library as it is now is where the steps start (after it is read).
    void start();
    // The library changed since it was last noted: a step, named for what changed. False when nothing did.
    bool note();

    bool canUndo() const { return m_stack.canUndo(); }
    bool canRedo() const { return m_stack.canRedo(); }
    std::string undoText() const { return m_stack.undoText(); }
    std::string redoText() const { return m_stack.redoText(); }
    void undo();
    void redo();
    // Steps taken: counts each new step (not an undo or redo), to order them among other histories.
    long long serial() const { return m_serial; }
    int index() const { return m_stack.index(); }
    // A step taken, undone or done again.
    std::function<void()> onChanged;

    // What changed from `before` to `after`, in words: "Delete Part R1", "Change 3 Parts", "New Package SOT-23".
    static std::string describe(const JPLibraryStore::Contents& before, const JPLibraryStore::Contents& after);

private:
    struct State {
        JPLibraryStore::Contents library;
        std::string              text;   // its JSON (JPConfiguration::libraryJson), to tell a change
    };
    State now() const;
    void restore(const State& s);

    JPConfiguration&        m_config;
    std::function<void()>   m_restored;
    JUndoStack              m_stack;
    std::shared_ptr<State>  m_last;
    long long               m_serial = 0;
    bool                    m_recording = false;   // a step pushed is done already: no restore
};

} // inline namespace jf
