// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <j/app/JAppWindow.h>
#include <j/core/MenuSystem.h>

#include <functional>

inline namespace jf {

// Opens a panel's menu (a button's menu, a table's context menu) on the
// screen where the panel is. A panel gives a point in the window that holds
// it: the main window while docked, a floating dock's own window while
// floating (the dock declares itself with JWidget::setHostWindowOverride), so
// the point is turned to the screen from whichever window that is.
class JPlacerMenuOpener {
public:
    using Open = std::function<void(JMenu* menu, float x, float y)>;
    static Open from(JAppWindow& window, const JWidget* panel);
};

} // inline namespace jf
