// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerMenuOpener.h"

inline namespace jf {

JPlacerMenuOpener::Open JPlacerMenuOpener::from(JAppWindow& window, const JWidget* panel) {
    return [&window, panel](JMenu* menu, float x, float y) {
        if (!JMenuManager::instance().onOpenMenu) return;
        const JSceneGraph::JHostWindow* floating = panel ? panel->hostWindowOverride() : nullptr;
        const int sx = floating ? floating->screenX : window.windowX();
        const int sy = floating ? floating->screenY : window.windowY();
        JMenuManager::instance().onOpenMenu(menu, sx + int(x), sy + int(y), false, false);
    };
}

} // inline namespace jf
