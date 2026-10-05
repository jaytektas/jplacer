// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPTextView.h"

inline namespace jf {

JPTextView::JPTextView(JSceneGraph& graph) : JTextArea(graph, "", 0.f, 0.f) {}

bool JPTextView::handleKeyEvent(const JKeyEvent& ke) {
    using K = JKeyEvent::JKey;
    switch (ke.key) {
        // Moving and choosing.
        case K::Left: case K::Right: case K::Up: case K::Down: case K::Home: case K::End: case K::PageUp: case K::PageDown:
            return JTextArea::handleKeyEvent(ke);
        // Copying, and choosing all.
        case K::C: case K::A:
            return ke.ctrl && !ke.alt ? JTextArea::handleKeyEvent(ke) : false;
        default: return false;
    }
}

} // inline namespace jf
