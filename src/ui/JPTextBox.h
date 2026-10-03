// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <j/core/JTextArea.h>

#include <string>

inline namespace jf {

// Lines of text (a command of several lines) whose value changes as a
// whole: typing changes only the box; leaving it commits it (onCommitted,
// when it differs from the value), and Escape puts back the value it had.
// Return starts a new line.
class JPTextBox : public JTextArea {
public:
    JPTextBox(JSceneGraph& graph, const std::string& placeholder);

    // Show `text` as the value (not a commit).
    void setValue(const std::string& text);
    // How tall a box showing `lines` lines is.
    static float heightFor(int lines);

    jf::JSignal<std::string> onCommitted;

    bool handleKeyEvent(const JKeyEvent& ke) override;

protected:
    void onFocusEvent(bool focused) override;

private:
    std::string m_value;   // the value as last committed or set
};

} // inline namespace jf
