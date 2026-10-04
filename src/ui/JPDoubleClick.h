// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <j/core/JStyle.h>

#include <chrono>

inline namespace jf {

// A second click on the same item within the style's double-click time,
// for a list in a dialog window (whose clicks the framework does not time,
// as the main window's are).
class JPDoubleClick {
public:
    // A click on `item`: true when it makes a double-click.
    bool click(int item) {
        const auto now = std::chrono::steady_clock::now();
        const auto since = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_at).count();
        const bool twice = item == m_item && since <= JStyle::current().doubleClickMs;
        m_item = twice ? -1 : item;   // a third click starts again
        m_at = now;
        return twice;
    }

private:
    int                                   m_item = -1;
    std::chrono::steady_clock::time_point m_at;
};

} // inline namespace jf
