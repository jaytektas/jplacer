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
    // View > Selections in Tables: Linked (true) or Unlinked (JPlacerTableLinks).
    static constexpr const char* kTablesLinked     = "view.tablesLinked";
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
    // The Jog panel's steps, numbers apart: the distances a press moves (in
    // the System Units, kept apart for each, or degrees) and the speeds (%) a
    // key or Faster / Slower picks.
    // OpenPnP's Window > Multiple Window Style: the cameras and the machine
    // controls each in a window of their own (taken at start).
    static constexpr const char* kMultipleWindows    = "window.multipleWindowStyle";
    // The window as last left: its docks (where each is, docked or in a window of its own: JAppWindow's
    // dockLayoutText), the docks closed (their titles, a line each), and its place and size
    // ("x y width height maximized").
    static constexpr const char* kDockLayout         = "window.dockLayout";
    // How many copies of the settings and cells, one taken as jplacer starts, are kept (the oldest let go;
    // 0: none taken).
    static constexpr const char* kBackupsKept        = "backups.kept";
    // Vision debugging (JPVisionDebug): every pipeline run's pictures kept under the configuration directory.
    static constexpr const char* kVisionDebug        = "vision.debugPictures";
    // How much of it is kept (MB; 0: no limit), the oldest let go first.
    static constexpr const char* kVisionDebugLimitMb = "vision.debugPicturesLimitMb";
    static constexpr int         kVisionDebugLimitMbDefault = 1000;
    static constexpr const char* kClosedDocks        = "window.closedDocks";
    static constexpr const char* kWindowGeometry     = "window.geometry";
    // OpenPnP's Change Appearance: tables' rows shaded every other one.
    static constexpr const char* kAlternateRows      = "window.alternateRows";
    // OpenPnP's View > Language: a JPTranslations code, "en" to begin with (taken at start).
    static constexpr const char* kLanguage           = "view.language";
    // OpenPnP's View > System Units: "Millimeters" or "Inches" (taken at start).
    static constexpr const char* kSystemUnits        = "view.systemUnits";
    static constexpr const char* kJogDistances       = "jog.distances";
    static constexpr const char* kJogDistancesInches = "jog.distancesInches";
    static const char* jogDistancesKey();
    static constexpr const char* kJogSpeeds        = "jog.speeds";
    // The cell file (cells/<name>.json) opened last; opened again at start.
    static constexpr const char* kMachineCell      = "machine.cell";
    // The job (.jpjob) open last; opened again at start.
    static constexpr const char* kJobFile          = "job.file";
    // File > Open Recent Job: the jobs opened or saved last, newest first,
    // as "job.recent.0" to "job.recent.9" (OpenPnP's JobPanel.recentFiles).
    static constexpr const char* kJobRecent       = "job.recent.";

    // The Parts tab: the table's share of its height (OpenPnP's PartsPanel.dividerPosition).
    static constexpr const char* kPartsSplit       = "parts.split";
    // The Packages tab's, likewise (PackagesPanel.dividerPosition).
    static constexpr const char* kPackagesSplit    = "packages.split";
    // The Vision tab's, likewise (VisionSettingsPanel.dividerPosition).
    static constexpr const char* kVisionSplit      = "vision.split";
    // The Feeders tab's, likewise (FeedersPanel.dividerPosition).
    static constexpr const char* kFeedersSplit     = "feeders.split";
    // Issues & Solutions: the target milestone, the issues solved and
    // dismissed (their fingerprints, space between), what is shown, the divider.
    static constexpr const char* kIssuesMilestone     = "issues.milestone";
    static constexpr const char* kIssuesSolved        = "issues.solved";
    static constexpr const char* kIssuesDismissed     = "issues.dismissed";
    static constexpr const char* kIssuesShowSolved    = "issues.showSolved";
    static constexpr const char* kIssuesShowDismissed = "issues.showDismissed";
    static constexpr const char* kIssuesSplit         = "issues.split";
    // The Boards tab's boards over its placements (BoardsPanel.dividerPosition).
    static constexpr const char* kBoardsSplit      = "boards.split";
    // The Panels tab's panels over its definition (PanelsPanel.dividerPosition).
    static constexpr const char* kPanelsSplit      = "panels.split";
    // The Job tab's boards over its placements (JobPanel.dividerPosition).
    static constexpr const char* kJobSplit         = "job.split";

    // ~/.config/jplacer/jplacer.json, or %APPDATA%\jplacer\jplacer.json.
    static std::string defaultPath();

    // Point JSettings at `path` and read it. A missing file is a first run, not
    // an error: every key has a default where it is read. A file there that
    // cannot be read is left as it is: nothing is written to it this run.
    static void load(const std::string& path);

    // Write JSettings back to the file load() named, never taking anything out
    // of it: every setting the file has is kept, but those taken out on
    // purpose this run (remove).
    static void save();
    // A setting back to its default: taken out, and out of the file at the next save.
    static void remove(const std::string& key);

    static bool updatesBeta();
    static bool visionDebug();
    static int  visionDebugLimitMb();
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
    // How much a camera's picture zooms by a notch of the wheel (JPCameraView::ZoomSensitivity, by name).
    static std::string cameraZoomKey(const std::string& cameraId);
    // How a camera's picture is drawn (JPCameraView::RenderingQuality, by name).
    static std::string cameraRenderingKey(const std::string& cameraId);
    // The key given to a function (JPKeyMap): "keys.<id>", absent for its
    // default, "none" for no key.
    static std::string keyFor(const std::string& functionId);
};

} // inline namespace jf
