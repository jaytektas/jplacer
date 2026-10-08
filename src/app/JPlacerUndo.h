// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <j/core/MenuSystem.h>

#include <functional>
#include <string>
#include <vector>

inline namespace jf {

// Edit's Undo and Redo, for every history (Machine Setup's, the library's): Undo takes back the last change made,
// wherever it was made, and says which ("Undo Delete Part R1"); Redo does again the last one taken back. Each
// history keeps its own steps; this keeps their order.
class JPlacerUndo {
public:
    // A history: what it can do, and how many new steps it has taken (never lowered but by starting again).
    struct Source {
        std::function<bool()>        canUndo, canRedo;
        std::function<std::string()> undoText, redoText;
        std::function<void()>        undo, redo;
        std::function<long long()>   serial;
    };
    // A history added; it calls changed() whenever its steps change.
    int add(Source s);
    // History `id`'s steps changed: a new step taken (put last), or the history started again.
    void changed(int id);
    void setItems(JMenuItem* undo, JMenuItem* redo);
    void undo();
    void redo();

private:
    void update();

    std::vector<Source>    m_sources;
    std::vector<long long> m_seen;      // each source's serial as last seen
    std::vector<int>       m_done;      // the sources of the steps that can be undone, oldest first
    std::vector<int>       m_undone;    // and of those undone, that can be done again, last undone last
    bool                   m_acting = false;
    JMenuItem*             m_undoItem = nullptr;
    JMenuItem*             m_redoItem = nullptr;
};

} // inline namespace jf
