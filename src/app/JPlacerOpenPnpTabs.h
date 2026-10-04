// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPlacerJob.h"
#include "JPlacerLayout.h"
#include "JPlacerMachine.h"
#include "JPlacerViewerDock.h"

#include "ui/JPBoardsPanel.h"
#include "ui/JPFeedersPanel.h"
#include "ui/JPJobPanel.h"
#include "ui/JPPanelsPanel.h"
#include "ui/JPPackagesPanel.h"
#include "ui/JPPartsPanel.h"

#include <j/app/JAppWindow.h>
#include <j/core/DockWidget.h>
#include <j/core/JLabel.h>
#include <j/core/JProgressBar.h>

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

inline namespace jf {

// OpenPnP's tabs in the work area (Job, Panels, Boards, Parts, Packages, Feeders so far), each a
// dock, kept in step with the job and its configuration: an edit on one is
// saved and the others shown again. A board's own file is saved as
// OpenPnP saves it: on File > Save Configuration, on quitting and on its
// removal, each changed one asked about.

class JPlacerOpenPnpTabs {
public:
    JPlacerOpenPnpTabs(JAppWindow& window, JSceneGraph& graph, JPlacerJob& job, JPlacerMachine& machine);
    ~JPlacerOpenPnpTabs();

    // Brings a tab forward by its title; false when there is none.
    bool showDock(const std::string& title);

    // File > Save Configuration: the configuration, and each changed board
    // asked about ("Save <file>?" Yes, No, Cancel); `then` runs after.
    void saveConfiguration(std::function<void()> then = {});
    // The window may close: false while changed boards are asked about
    // (the window is asked to close again after).
    bool mayClose();
    // File > Import Board: an importer's dialog for the Boards tab's chosen board.
    const std::vector<std::unique_ptr<JPBoardImporter>>& importers() const;
    void importBoard(const JPBoardImporter& importer);

private:
    // Asks about one changed board or panel, saving it on Yes; `then` after any answer.
    void confirmSave(JPPlacementsHolder& holder, std::function<void()> then);
    void confirmSaveAll(std::vector<std::string> files, std::function<void()> then);
    void changed();
    // The known board (its shared definition) a pointer names.
    std::shared_ptr<JPBoard> boardOf(const JPBoard* board) const;

    JAppWindow&                   m_window;
    JPlacerJob&                   m_job;
    JPlacerMachine&               m_machine;
    JPlacerLayout&                m_layout;
    int                           m_watch = 0;
    std::unique_ptr<JPPartsPanel>    m_parts;
    std::unique_ptr<JDockWidget>     m_partsDock;
    std::unique_ptr<JPPackagesPanel> m_packages;
    std::unique_ptr<JDockWidget>     m_packagesDock;
    std::unique_ptr<JPFeedersPanel>  m_feeders;
    std::unique_ptr<JDockWidget>     m_feedersDock;
    std::unique_ptr<JPBoardsPanel>   m_boards;
    std::unique_ptr<JDockWidget>     m_boardsDock;
    std::unique_ptr<JPlacerViewerDock> m_boardViewer;
    std::unique_ptr<JPJobPanel>      m_jobPanel;
    std::unique_ptr<JDockWidget>     m_jobDock;
    std::unique_ptr<JPlacerViewerDock> m_jobViewer;
    // The status line's placements done ("Placements: 3 / 10 Total | …") and its bar.
    std::unique_ptr<JLabel>          m_placedLabel;
    std::unique_ptr<JProgressBar>    m_placedBar;
    std::unique_ptr<JPPanelsPanel>   m_panels;
    std::unique_ptr<JDockWidget>     m_panelsDock;
    std::unique_ptr<JPlacerViewerDock> m_panelViewer;
    bool                             m_closing = false;
};

} // inline namespace jf
