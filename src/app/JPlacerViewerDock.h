// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPlacerLayout.h"

#include "model/JPPlacementsHolderLocation.h"
#include "ui/JPPlacementsViewer.h"

#include <j/core/DockWidget.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

inline namespace jf {

// A board's or panel's viewer (OpenPnP's Board Viewer and Panel Viewer
// windows) as a dock: opened beside the cameras, where it can be dragged
// out on its own, and kept showing whichever board or panel its tab has
// chosen (named over its options), drawn again as it changes.
class JPlacerViewerDock {
public:
    // `kind`: "Board", "Panel" or "Job", for its title.
    JPlacerViewerDock(JSceneGraph& graph, JPlacerLayout& layout, std::string kind);
    ~JPlacerViewerDock();

    // Shows `holder` (a definition) and brings the dock forward.
    void open(std::shared_ptr<JPPlacementsHolder> holder);
    // Follows its tab's choice while open (none: left as it is, as OpenPnP's).
    void follow(std::shared_ptr<JPPlacementsHolder> holder);
    void regenerate();
    // The job's viewer: the job (its root, which the job keeps) with the
    // boards and panels chosen in the Job tab; opened, and followed.
    void openJob(JPPanelLocation* root, std::string name, std::vector<const JPPlacementsHolderLocation*> chosen);
    void followJob(JPPanelLocation* root, std::string name, std::vector<const JPPlacementsHolderLocation*> chosen);
    bool isOpen() const { return m_open; }
    JPPlacementsViewerCanvas& canvas() { return m_viewer->canvas(); }

private:
    void show(std::shared_ptr<JPPlacementsHolder> holder);
    void showJob(JPPanelLocation* root, const std::string& name, std::vector<const JPPlacementsHolderLocation*> chosen);
    void place();

    JPlacerLayout&                               m_layout;
    std::string                                  m_kind;
    std::unique_ptr<JPPlacementsHolderLocation>  m_root;      // a board's or panel's, made here
    JPPanelLocation*                             m_jobRoot = nullptr;   // the job's, its own
    std::unique_ptr<JPPlacementsViewer>          m_viewer;
    std::unique_ptr<JDockWidget>                 m_dock;
    bool                                         m_open = false;
};

} // inline namespace jf
