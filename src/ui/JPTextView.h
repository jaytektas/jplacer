// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <j/core/JTextArea.h>

inline namespace jf {

// Text to read, not to change: wrapped and scrolled as a text area, its
// caret moved and its text chosen and copied, nothing typed into it (as
// OpenPnP's stage description).
class JPTextView : public JTextArea {
public:
    explicit JPTextView(JSceneGraph& graph);

    bool handleKeyEvent(const JKeyEvent& ke) override;
};

} // inline namespace jf
