// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <j/app/JAppWindow.h>
#include <j/core/SceneGraph.h>

inline namespace jf {

class JPlacerApp;

// Builds the menu bar, so the whole menu tree can be read in one sitting.
//
// THE SKELETON. Entries for work that does not exist yet (jobs, the machine)
// are present but DISABLED, never wired to a handler that does nothing: a
// greyed item says "not yet" honestly, while a live one that ignores the click
// reads as a bug. Each is enabled in the same change that implements it.
class JPlacerMenuBuilder {
public:
    static void build(JAppWindow& window, JSceneGraph& graph, JPlacerApp& app);
};

} // inline namespace jf
