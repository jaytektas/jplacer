// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

inline namespace jf {

// jplacer's entry in the desktop's applications menu, with its icon.
//
// An AppImage is one file the person downloads and runs; nothing installs it,
// so on its own it has no menu entry and no icon in the launcher or the dock.
// When jplacer runs as an AppImage it puts itself in the menu (per user, under
// ~/.local/share, no root), and re-points the entry at the AppImage on every
// start, so moving the file does not leave a dead launcher behind. The self-
// updater replaces the AppImage in place, so an update keeps the same entry.
//
// The entry and icon are the ones packed into the AppImage
// (packaging/jplacer.desktop, packaging/jplacer.svg), with Exec rewritten to
// the AppImage's path -- so there is one copy of each, not a second spelling
// in the code.
//
// A source build is not an AppImage and is left alone: a launcher pointing at
// a build directory is not something to create behind a developer's back.
class JPlacerLauncher {
public:
    // Running as an AppImage on Linux: there is something to install.
    static bool supported();

    // Write or refresh the menu entry and icon. Writes only what differs, so an
    // ordinary start touches nothing. True when the entry is in place afterwards.
    static bool install();

    // Take the entry and icon out again.
    static void remove();
};

} // inline namespace jf
