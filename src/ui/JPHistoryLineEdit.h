// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <j/core/JLineEdit.h>

#include <deque>
#include <string>

inline namespace jf {

// A line edit that remembers what was entered (OpenPnP's G-code console's
// history): Up brings back the one before, Down the one after, the newest
// last; a line the same as the one before it is kept once.
class JPHistoryLineEdit : public JLineEdit {
public:
    static constexpr size_t kMostKept = 50;   // OpenPnP's

    JPHistoryLineEdit(JSceneGraph& graph, const std::string& placeholder);
    // `line` kept as the newest (unless the same as it), and the history read from the newest again.
    void remember(const std::string& line);
    bool handleKeyEvent(const JKeyEvent& ke) override;

private:
    std::deque<std::string> m_history;   // newest first
    int                     m_at = -1;   // the one shown (-1: none, what is being typed)
};

} // inline namespace jf
