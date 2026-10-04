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
    static constexpr const char* kSetupTreeShare   = "setup.treeWidthShare";
    // How much the log says (JPLogLevels::toText), and whether the console
    // shows the controllers' traffic.
    static constexpr const char* kLogLevels      = "log.levels";
    static constexpr const char* kConsoleTraffic = "console.traffic";
    // The Jog panel's choices: the tool, the distance (its index) and the speed (a share).
    static constexpr const char* kJogTool          = "jog.tool";
    static constexpr const char* kJogDistance      = "jog.distance";
    static constexpr const char* kJogSpeed         = "jog.speed";
    static constexpr const char* kJogStepThrough   = "jog.stepThrough";   // tip changes asked step by step
    // The Jog panel's steps, numbers apart: the distances a press moves (mm
    // or degrees) and the speeds (%) a key or Faster / Slower picks.
    static constexpr const char* kJogDistances     = "jog.distances";
    static constexpr const char* kJogSpeeds        = "jog.speeds";
    // The cell file (cells/<name>.json) opened last; opened again at start.
    static constexpr const char* kMachineCell      = "machine.cell";
    // The job (.jpjob) open last; opened again at start.
    static constexpr const char* kJobFile          = "job.file";

    // The Parts tab: the table's share of its height (OpenPnP's PartsPanel.dividerPosition).
    static constexpr const char* kPartsSplit       = "parts.split";
    // The Packages tab's, likewise (PackagesPanel.dividerPosition).
    static constexpr const char* kPackagesSplit    = "packages.split";
    // The Boards tab's boards over its placements (BoardsPanel.dividerPosition).
    static constexpr const char* kBoardsSplit      = "boards.split";

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
    // The reticle over a camera's picture (JPReticle::toText).
    static std::string cameraReticleKey(const std::string& cameraId);
    // The key given to a function (JPKeyMap): "keys.<id>", absent for its
    // default, "none" for no key.
    static std::string keyFor(const std::string& functionId);
};

} // inline namespace jf
