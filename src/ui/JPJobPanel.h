// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPBoardLocationProcess.h"
#include "JPGroupFrame.h"
#include "JPInstructions.h"
#include "JPIconButton.h"
#include "JPJobPlacementsPanel.h"
#include "JPLocationsTableModel.h"
#include "JPPanelDefinitionPanel.h"
#include "JPTable.h"

#include "model/JPConfiguration.h"
#include "model/JPJob.h"

#include <j/core/JContainer.h>
#include <j/core/JTabWidget.h>
#include <j/core/MenuSystem.h>
#include <j/core/Splitter.h>

#include <j/core/ToolBar.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

inline namespace jf {

// The Job tab, as OpenPnP's JobPanel: the job's Boards (its boards and
// panels, nested, where each lies, its side, enabled and fiducial check)
// over the chosen one's Placements (JPJobPlacementsPanel: a board's own
// placements, and its imports, revisions, parts and viewer) and, a panel
// chosen, the panel itself beside them (JPPanelDefinitionPanel: its children
// and fiducials), what every use of it shares. A board or panel is made, or opened, only into the job (Add
// Board/Panel: New, Existing, or a copy of the chosen one). The toolbar:
// Start, Step, Stop, Defer or Alert Errors, Add Board/Panel (a new or
// existing board or panel), Remove Board(s)/Panel(s), Move Camera To (and
// To the Next) Board Location, Move Tool To Board Location, Capture Camera
// and Tool Location, Multiple Point Board Location, Fiducial Check, View
// Job; the right-click Set Side, Set Enabled, Set Check Fids.
class JPJobPanel : public JContainer {
public:
    using Tool = JPJobPlacementsPanel::Tool;
    // OpenPnP's JobPanel.State.
    enum class RunState { Stopped, Paused, Running, Pausing, Stopping };

    JPJobPanel(JSceneGraph& graph, JPConfiguration& config, std::function<JPJob*()> job, double split);

    // The job changed (to be shown as changed, other views told).
    std::function<void()> onChanged;
    std::function<void(JMenu*, float x, float y)> openMenu;
    std::function<std::optional<JPLocation>(Tool)> toolLocation;
    // Where a board or panel added starts (the machine's Default Board Location).
    std::function<JPLocation()> defaultLocation;
    std::function<void(Tool, const JPLocation&)> moveTool;
    std::function<void(const std::string& title, const std::string& what, std::function<void(std::string)> chosen)>
        chooseExisting;
    // View Job, showing the job (and the boards and panels chosen).
    std::function<void(std::vector<const JPPlacementsHolderLocation*> chosen)> onViewJob;
    // The chosen boards and panels changed (the job viewer follows them).
    std::function<void(std::vector<const JPPlacementsHolderLocation*> chosen)> onSelectionChanged;
    // One board or panel chosen in the table (for the tables linked to it, View > Selections in Tables).
    std::function<void(const JPPlacementsHolderLocation&)> onLocationChosen;

    // Start (Pause, Resume), Step and Stop; Fiducial Check on the chosen board or panel.
    std::function<void()> onStartPauseResume, onStep, onStop;
    std::function<void(JPPlacementsHolderLocation*)> onFiducialCheck;
    // The boards chosen photographed (a panel's, all its boards).
    std::function<void(std::vector<JPPlacementsHolderLocation*>)> onPhotograph;
    // How the job runs now, and whether the machine is connected: the run
    // buttons follow, as OpenPnP's updateJobActions has them.
    void setRunState(RunState s);
    // A second Start (Pause, Resume), Step and Stop in `bar` (the window's toolbar, by the machine's power and home),
    // as the Job tab's are: their icons, tooltips and whether they can be pressed kept the same.
    void addRunButtons(JToolBar& bar);
    // A photo being taken (asked for): Stop stops it.
    void setPhotographing(bool on);
    void setMachineEnabled(bool on);
    // The Job menu's Start, Step and Stop, kept as the buttons are.
    void setMenuItems(JMenuItem* start, JMenuItem* step, JMenuItem* stop);
    // Job > Reset All Placed: every placement of the job not placed.
    void resetAllPlaced();
    // Edit > Add Board: a new or existing board or panel added to the job;
    // Edit > Remove Board, Capture Tool Location (as the toolbar's), their
    // entries enabled as the buttons are.
    void addNew(bool panel);
    void addExisting(bool panel);
    void removeSelected();
    void captureTool();
    void setEditItems(JMenuItem* remove, JMenuItem* captureTool);
    RunState runState() const { return m_runState; }

    JPJobPlacementsPanel& placements() { return *m_placements; }
    // The chosen board's tools (in its Placements), and the chosen panel itself (the Panel tab).
    JPBoardTools& board() { return m_placements->boardTools(); }
    JPPanelDefinitionPanel& panel() { return *m_panel; }
    // The chosen board or panel itself changed (not the job: it is saved to its own file).
    std::function<void()> onDefinitionChanged;
    // The board or panel whose editor is shown changed (none: none chosen): the viewers follow.
    std::function<void(JPBoard*)> onBoardShown;
    std::function<void(JPPanel*)> onPanelShown;
    // Asks whether to save a changed board or panel (its copy is made from it as saved).
    std::function<void(JPPlacementsHolder&, std::function<void()> then)> confirmSave;
    // The Placements brought forward (File > Import Placements: the chosen board's).
    void showBoard() { m_lower->setActiveTab(0); }
    // Add Board/Panel's copy: the chosen board or panel saved as a new file (asked for), added to the job.
    void copyChosen();
    // The board or panel of a unique id ("Pnl1⇒Brd2") chosen, and a placement on it.
    void select(const std::string& uniqueId, const std::string& placementId);
    // OpenPnP's selectPlacementsHolderLocation: the board or panel of the job
    // that is an instance of `definition` chosen (none: none chosen).
    void selectLocation(const JPPlacementsHolderLocation* definition);
    // Another job, or the job changed elsewhere: shown again.
    void refresh();
    double split() const;

    // The boards and panels chosen.
    std::vector<JPPlacementsHolderLocation*> selections() const;
    // OpenPnP's instructions panel across the top of the tab: shown with a
    // step of a process, gone with none.
    void showInstructions(const std::string& title, const std::string& text, const std::string& proceedLabel,
                          std::function<void()> onCancel, std::function<void()> onProceed);
    void hideInstructions();

private:
    void selectionChanged();
    // The chosen board's tools, or the chosen panel's tab, shown.
    void showDefinition();
    void updateJobActions();
    void buildMenu();
    void showAddMenu();
    void addBoard(const std::string& path, const char* errorTitle);
    void addPanel(const std::string& path, const char* errorTitle);
    void moveTo(Tool tool, bool next);
    void captureCamera();
    void changed();

    JPConfiguration&                    m_config;
    std::function<JPJob*()>             m_job;
    JPLocationsTableModel               m_model;
    JPTable*                            m_table = nullptr;
    JPJobPlacementsPanel*               m_placements = nullptr;
    JSplitter*                          m_split = nullptr;
    std::unique_ptr<JContainer>         m_boardsPane, m_placementsPane, m_inJobPage, m_panelPage;
    bool                                m_panelShown = false;   // the Panel tab there
    // Placements, and Panel beside it while a panel is chosen (no tab strip without it).
    JTabWidget*                         m_lower = nullptr;
    float                               m_stripThickness = 0;   // the tab strip's, while it shows
    JPPanelDefinitionPanel*             m_panel = nullptr;
    JPBoard*                            m_shownBoard = nullptr;   // the board whose tools are shown (the viewer told)
    RunState                            m_runState = RunState::Stopped;
    bool                                m_machineEnabled = false;
    JMenuItem*                          m_startItem = nullptr;
    JMenuItem*                          m_stepItem = nullptr;
    JMenuItem*                          m_stopItem = nullptr;
    JMenuItem*                          m_removeItem = nullptr;
    JMenuItem*                          m_captureToolItem = nullptr;
    JPIconButton*                       m_start = nullptr;
    JPIconButton*                       m_step = nullptr;
    JPIconButton*                       m_stop = nullptr;
    JPIconButton*                       m_errors = nullptr;
    JPIconButton*                       m_add = nullptr;
    JPIconButton*                       m_remove = nullptr;
    JPIconButton*                       m_cameraTo = nullptr;
    JPIconButton*                       m_cameraNext = nullptr;
    JPIconButton*                       m_toolTo = nullptr;
    JPIconButton*                       m_captureCamera = nullptr;
    JPIconButton*                       m_captureTool = nullptr;
    JPIconButton*                       m_twoPoint = nullptr;
    JPIconButton*                       m_fiducialCheck = nullptr;
    JPIconButton*                       m_photograph = nullptr;
    std::unique_ptr<JPIconButton>       m_barStart, m_barStep, m_barStop;   // the toolbar's (addRunButtons)
    bool                                m_photographing = false;
    std::unique_ptr<JMenu>              m_addMenu, m_menu;
    JContainer*                         m_instructionsHolder = nullptr;
    std::unique_ptr<JPInstructions>     m_instructions;
    bool                                m_instructionsShown = false;
    std::unique_ptr<JPBoardLocationProcess> m_locating;   // Multiple Point Board Location under way
    std::vector<std::unique_ptr<JMenu>> m_subMenus;
};

} // inline namespace jf
