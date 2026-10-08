// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPlacerJob.h"
#include "JPlacerJobRun.h"
#include "JPlacerLayout.h"
#include "JPlacerMachine.h"
#include "JPlacerPipelines.h"
#include "JPlacerOcrRegionSetup.h"
#include "JPlacerStripAutoSetup.h"
#include "JPlacerTableLinks.h"
#include "JPlacerVisionTests.h"
#include "JPlacerViewerDock.h"

#include "ui/JPBoardsPanel.h"
#include "setup/JPSolutions.h"
#include "ui/JPFeedersPanel.h"
#include "ui/JPIssuesPanel.h"
#include "ui/JPLogPanel.h"
#include "ui/JPVisionSettingsPanel.h"
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

// OpenPnP's tabs in the work area (Job, Panels, Boards, Parts, Packages, Vision, Feeders so far), each a
// dock, kept in step with the job and its configuration: an edit on one is
// saved and the others shown again. A board's own file is saved as
// OpenPnP saves it: on File > Save Configuration, on quitting and on its
// removal, each changed one asked about.

class JPlacerOpenPnpTabs {
public:
    JPlacerOpenPnpTabs(JAppWindow& window, JSceneGraph& graph, JPlacerJob& job, JPlacerMachine& machine);
    ~JPlacerOpenPnpTabs();

    JPJobPanel& jobPanel() { return *m_jobPanel; }
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
    // From a placement file and its BOM, into the Boards tab's chosen board (JPlacerCplBomImportDialog).
    void importCplBom();
    // Job > Shortages…: the job's parts against the stock (JPlacerShortagesDialog).
    void openShortages();
    // Job > Runs…: the runs of jobs (JPlacerRunsDialog).
    void openRuns();
    // Job > Check Job…: the job's data checked before a run (JPlacerJobRun::checkJob).
    void checkJob();
    // Job > Plan…: the job's part groups in run order (JPlacerPlanDialog).
    void openPlan();

private:
    // A script's request of the job (JPlacerMachine::onScriptJobRequest).
    JJson scriptJobRequest(const JJson& request);
    // Asks about one changed board or panel, saving it on Yes; `then` after any answer.
    void confirmSave(JPPlacementsHolder& holder, std::function<void()> then);
    // A board saved for the first time as jplacer's file (it was OpenPnP's, `from`): the job follows it.
    void boardMoved(const JPBoard& board, const std::string& from);
    void confirmSaveAll(std::vector<std::string> files, std::function<void()> then);
    void changed();
    // The machine's default vision settings (bottom vision's, the fiducial locator's).
    std::pair<std::string, std::string> machineVisionDefaults() const;
    // The machine's PhotonFeederData actuator made when a Photon feeder needs it.
    void ensurePhotonActuator();
    // A feeder's pipeline (`element`) edited on the head camera, kept when saved.
    void editFeederPipeline(const std::string& feederId, const std::string& element);
    // A Bamboo feeder's, as OpenPnP's: asked first whether to move the camera
    // over the middle of its holes (its vision location), when it is not there.
    void editTapePipeline(const std::string& feederId);
    // The known board (its shared definition) a pointer names.
    std::shared_ptr<JPBoard> boardOf(const JPBoard* board) const;

    JAppWindow&                   m_window;
    JPlacerJob&                   m_job;
    JPlacerMachine&               m_machine;
    JPlacerLayout&                m_layout;
    int                           m_watch = 0;
    bool                          m_refreshPending = false;
    std::unique_ptr<JPPartsPanel>    m_parts;
    std::unique_ptr<JDockWidget>     m_partsDock;
    std::unique_ptr<JPPackagesPanel> m_packages;
    std::unique_ptr<JDockWidget>     m_packagesDock;
    std::unique_ptr<JPVisionSettingsPanel> m_vision;
    std::unique_ptr<JDockWidget>     m_visionDock;
    JPSolutions                      m_solutions;
    // Gone with the tabs: what was posted for later does nothing then.
    std::shared_ptr<bool>            m_alive = std::make_shared<bool>(true);
    std::unique_ptr<JPIssuesPanel>   m_issues;
    std::unique_ptr<JDockWidget>     m_issuesDock;
    std::unique_ptr<JPLogPanel>      m_log;
    std::unique_ptr<JDockWidget>     m_logDock;
    std::unique_ptr<JPFeedersPanel>  m_feeders;
    JPlacerPipelines                 m_pipelines;
    std::unique_ptr<JDockWidget>     m_feedersDock;
    std::unique_ptr<JPBoardsPanel>   m_boards;
    std::unique_ptr<JDockWidget>     m_boardsDock;
    std::unique_ptr<JPlacerViewerDock> m_boardViewer;
    std::unique_ptr<JPJobPanel>      m_jobPanel;
    std::unique_ptr<JDockWidget>     m_jobDock;
    std::unique_ptr<JPlacerViewerDock> m_jobViewer;
    std::unique_ptr<JPlacerJobRun>   m_jobRun;
    std::unique_ptr<JPlacerVisionTests> m_visionTests;
    std::unique_ptr<JPlacerStripAutoSetup> m_autoSetup;
    std::unique_ptr<JPlacerOcrRegionSetup> m_ocrRegion;
    std::unique_ptr<JPlacerTableLinks> m_links;
    // The status line's placements done ("Placements: 3 / 10 Total | …") and its bar.
    std::unique_ptr<JLabel>          m_placedLabel;
    std::unique_ptr<JProgressBar>    m_placedBar;
    std::unique_ptr<JPPanelsPanel>   m_panels;
    std::unique_ptr<JDockWidget>     m_panelsDock;
    std::unique_ptr<JPlacerViewerDock> m_panelViewer;
    bool                             m_closing = false;
};

} // inline namespace jf
