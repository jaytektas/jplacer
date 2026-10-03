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
    // The look: the theme (0 dark, 1 light, 2 as the desktop is set) and the
    // interface scale (0: as the screen asks, else 1 = 100 %). See
    // JPlacerAppearance.
    static constexpr const char* kTheme            = "appearance.theme";
    static constexpr const char* kUiScale          = "ui.scale";
    // Machine Setup's divider: the tree's share of the room over the settings.
    static constexpr const char* kSetupTreeShare   = "setup.treeShare";
    // The Jog panel's choices: the tool, the distance (its index) and the speed (a share).
    static constexpr const char* kJogTool          = "jog.tool";
    static constexpr const char* kJogDistance      = "jog.distance";
    static constexpr const char* kJogSpeed         = "jog.speed";
    // The cell file (cells/<name>.json) opened last; opened again at start.
    static constexpr const char* kMachineCell      = "machine.cell";
    // The board on the machine: its pick-and-place file, the side up
    // ("top" or "bottom"), where it is (its map to the machine, six numbers
    // "a b c d tx ty", see JPAffine2D) and whether that was measured by its
    // fiducials (true) or is a starting guess.
    static constexpr const char* kBoardFile        = "board.file";
    static constexpr const char* kBoardSide        = "board.side";
    static constexpr const char* kBoardPlace       = "board.place";
    static constexpr const char* kBoardMeasured    = "board.measured";

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
    static int theme();
    static double uiScale();
    static std::string machineCell();

    // How a camera's picture is shown, each camera its own: straightened
    // (true) or as taken. "camera.<id>.straight".
    static std::string cameraStraightKey(const std::string& cameraId);
};

} // inline namespace jf
