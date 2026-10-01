// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <string>

inline namespace jf {

// Where jplacer keeps its preferences, and what they are called.
//
// The values live in JSettings::instance() — the framework's updater reads its
// beta switch and its "don't ask about this version" from there directly — so
// this class does not hold a copy. It owns the file the singleton is backed by
// and the key names, so no other file spells a key out.
class JPlacerSettings {
public:
    // Turned on: the update check offers beta releases as well as full ones.
    static constexpr const char* kUpdatesBeta      = "updates.beta";
    // Turned off: jplacer does not look for a newer version when it opens.
    // Help > Check for Updates still does.
    static constexpr const char* kUpdatesAtStartup = "updates.checkAtStartup";

    // Turned on: a menu can be dragged off into a window of its own. Off by
    // default.
    static constexpr const char* kTearOffMenus     = "ui.tearOffMenus";
    // Turned on (and running as an AppImage): jplacer keeps its entry in the
    // desktop's applications menu. See JPlacerLauncher.
    static constexpr const char* kLauncher         = "desktop.launcher";

    // ~/.config/jplacer/jplacer.json, or %APPDATA%\jplacer\jplacer.json.
    static std::string defaultPath();

    // Point JSettings at `path` and read it. A missing file is a first run, not
    // an error: every key has a default where it is read.
    static void load(const std::string& path);

    // Write JSettings back to the file load() named.
    static void save();

    static bool updatesBeta();
    static bool updatesAtStartup();
    static bool tearOffMenus();
    static bool launcher();
};

} // inline namespace jf
