// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "model/JPConfiguration.h"

#include <j/core/DockWidget.h>

#include <functional>
#include <string>

inline namespace jf {

class JPBoardsPanel;
class JPFeedersPanel;
class JPJobPanel;
class JPPackagesPanel;
class JPPanelsPanel;
class JPPartsPanel;
class JPVisionSettingsPanel;

// OpenPnP's View > Selections in Tables, Linked: what is chosen in one tab's
// table chooses what goes with it in the others. A board or panel of the job
// chooses its board on the Boards tab or its panel on the Panels tab (and the
// other way round); a placement chooses the same placement there, and its
// part on the Parts tab; a part chooses its package, its feeder and the
// vision settings it uses; a feeder chooses its part, and so on from there.
// Only what is chosen on the tab in front leads (OpenPnP's selected tab), so
// what a link chooses leads nowhere further.
class JPlacerTableLinks {
public:
    struct Tabs {
        JPJobPanel&            job;
        JPBoardsPanel&         boards;
        JPPanelsPanel&         panels;
        JPPartsPanel&          parts;
        JPPackagesPanel&       packages;
        JPFeedersPanel&        feeders;
        JPVisionSettingsPanel& vision;
        JDockWidget&           jobDock;
        JDockWidget&           boardsDock;
        JDockWidget&           panelsDock;
        JDockWidget&           partsDock;
        JDockWidget&           feedersDock;
    };

    // `linked`: whether the tables are linked now (the setting, read each time).
    JPlacerTableLinks(Tabs tabs, JPConfiguration& config, std::function<bool()> linked);

private:
    // Whether a choice on the tab of `dock` leads: linked, the tab in front, and not a link's own choosing.
    bool leads(const JDockWidget& dock) const;
    // OpenPnP's selectPartInTableAndUpdateLinks: the part, its package, its feeder and its vision settings,
    // each where the part lives: a board's part (`board`, its board part `boardPartKey`) its board's rows
    // (its own package, or its copy of the library's), a library part the library's.
    void partAndLinks(const JPBoard* board, const std::string& boardPartKey, const std::string& partId, bool choosePart);
    // A part by id where it lives: a board's own (its id the board's), else the library's.
    void partAndLinks(const std::string& partId, bool choosePart);
    void choose(const std::function<void()>& linkedChoices);

    void jobLocationChosen(const JPPlacementsHolderLocation& l);
    void jobPlacementChosen(const JPPlacement* p);
    void boardPlacementChosen(const JPPlacement* p);
    void panelChildChosen(const JPPlacementsHolderLocation& child);
    void panelFiducialChosen(const JPPlacement& f);

    Tabs                  m_tabs;
    JPConfiguration&      m_config;
    std::function<bool()> m_linked;
    bool                  m_linking = false;
};

} // inline namespace jf
