// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <j/core/JLineEdit.h>

#include <string>

inline namespace jf {

// A line of text whose value changes as a whole, like a spin box's: typing
// changes only the field; Return, Tab or leaving the field commits it
// (onCommitted, when it differs from the value), and Escape puts back the
// value it had.
class JPTextField : public JLineEdit {
public:
    explicit JPTextField(JSceneGraph& graph);

    // Show `text` as the value (not a commit).
    void setValue(const std::string& text);

    jf::JSignal<std::string> onCommitted;

    bool handleKeyEvent(const JKeyEvent& ke) override;

protected:
    void onFocusEvent(bool focused) override;

private:
    void commit();

    std::string m_value;   // the value as last committed or set
};

} // inline namespace jf
