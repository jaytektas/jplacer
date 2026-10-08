// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPSetupHistory.h"

#include <j/core/Command.h>

#include <memory>

inline namespace jf {

JPSetupHistory::JPSetupHistory(std::function<void(const State&)> restore) : m_restore(std::move(restore)) {
    m_stack.setUndoLimit(kSteps);
}

void JPSetupHistory::record(const std::string& what, const std::string& key, State before, State after) {
    // A run of changes to the same thing shares an id, and the stack joins them.
    if (key.empty() || key != m_key) {
        ++m_id;
        ++m_serial;
    }
    m_key = key;
    auto b = std::make_shared<const State>(std::move(before));
    auto a = std::make_shared<const State>(std::move(after));
    m_recording = true;   // push() does the change; it is done already
    m_stack.push(new JFunctionCommand(
        what,
        [this, a] { if (!m_recording) m_restore(*a); },
        [this, b] { m_restore(*b); },
        key.empty() ? -1 : m_id));
    m_recording = false;
    if (onChanged) onChanged();
}

void JPSetupHistory::seal() {
    m_key.clear();
}

void JPSetupHistory::undo() {
    if (!canUndo()) return;
    m_stack.undo();
    seal();
    if (onChanged) onChanged();
}

void JPSetupHistory::redo() {
    if (!canRedo()) return;
    m_stack.redo();
    seal();
    if (onChanged) onChanged();
}

} // inline namespace jf
