// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPlacerBoard.h"
#include "JPlacerCameraTasks.h"
#include "JPlacerLayout.h"

#include "machine/JPCell.h"
#include "ui/JPCameraPanel.h"
#include "ui/JPConnectIcon.h"
#include "ui/JPHomeIcon.h"
#include "ui/JPJogPanel.h"
#include "ui/JPMachineSetupPanel.h"
#include "ui/JPPositionReadout.h"

#include <j/app/JAppWindow.h>
#include <j/core/DockWidget.h>
#include <j/core/JContainer.h>
#include <j/core/MenuSystem.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

inline namespace jf {

// The machine jplacer is working with: the open cell (cells/<name>.json),
// its panels, each in a dock of its own where JPlacerLayout puts it (a
// camera each; Jog, Actuators; Board, Machine Setup, Machine; Console), the
// chosen tool's position in the status bar, the strip across the window
// saying what state it is in, and the Machine menu's actions on it.
//
// The cell opened last is opened again at start (JPlacerSettings::kMachineCell).
class JPlacerMachine {
public:
    JPlacerMachine(JAppWindow& window, JSceneGraph& graph);
    ~JPlacerMachine();

    JPlacerMachine(const JPlacerMachine&)            = delete;
    JPlacerMachine& operator=(const JPlacerMachine&) = delete;

    // The Machine menu's entries this class enables and disables.
    void setMenuItems(JMenuItem* connect, JMenuItem* disconnect, JMenuItem* home, JMenuItem* park);
    // Edit's Undo and Redo, which step through Machine Setup's changes.
    void setEditItems(JMenuItem* undo, JMenuItem* redo);
    void undo();
    void redo();

    void chooseCell();          // Machine > Open Cell…
    // Machine > Import OpenPnP Machine…: offers OpenPnP's usual machine.xml
    // when there is one, else (or on request) a file to choose.
    void importOpenPnp();
    void connect();
    void disconnect();
    void home();                // Machine > Home All Axes
    void park();                // Machine > Park Head
    // Bring the dock titled `title` to the front of its tab group (shown
    // again if it was closed). False when there is no such dock.
    bool showDock(const std::string& title);
    // Machine Setup in front, showing the node at `path` (JPSetupTree).
    void showSetup(const std::string& path);
    // Where the docks live, and View's entries for them.
    JPlacerLayout& layout() { return m_layout; }

    // The directory cell files are kept in.
    static std::string cellsDir();

private:
    bool openCell(const std::string& path, std::string& error);
    void importFrom(const std::string& machineXml);
    void setPort(const std::string& driverId, const std::string& port);
    // A change in Machine Setup (and a port chosen): the running machine
    // takes `cell` (with the calibrations and squareness measured meanwhile,
    // see JPCell::reconfigure), the panels it changes are made again, and it
    // is kept in the cell file. False when the machine is moving (it is
    // tried again shortly).
    bool applySetup(JPCellConfig cell);
    // Follow the open cell's signals (the menu, the strip, the status bar).
    void watchCell();
    // Correct the squareness of the gantry moving `mount` by `xPerY` more (from
    // a board), and keep it in the cell file.
    void squareMachine(const JPMountConfig& mount, double xPerY);
    void updateMenu();
    // The connect and home icons follow the cell; the strip across the window
    // is kept for what is critical (ALARM, CONNECTION LOST) and a failure goes
    // to the status bar.
    void showState();
    // What stays when the panels are made again: nothing (another cell),
    // Machine Setup (its changes are what is being taken), or the cameras too
    // (and the Board, which works from them) when their settings did not change.
    enum class Keep { Nothing, Setup, SetupAndCameras };
    // One dock per panel, made the first time a cell opens; a new cell gets
    // new panels in the same docks, so where the person put them is kept.
    // The cameras' docks are the cell's own, made new with it.
    void buildPanels(Keep keep = Keep::Nothing);
    void buildCameras();
    std::unique_ptr<JPMachineSetupPanel> makeSetup();
    // A camera's light is on while its camera runs (on screen) and the
    // machine is connected; while not connected, a camera with a light says
    // why its picture is dark.
    void lightCameras();
    void bringForward(JPCameraPanel& camera);
    void dropPanels(Keep keep = Keep::Nothing);
    void updateEditItems();
    // Machine Setup's buttons: Visual Test, Visual Home, a camera's Start Calibration.
    void setupAction(const std::string& path, const std::string& action);
    // What Machine Setup's place buttons use: the camera on the head, or the
    // nozzle chosen on the Jog panel (else the first). Null when there is none.
    const JPMountConfig* toolMount(JPSetupForm::Tool tool) const;
    // Connected and homed; else the status bar says what is needed first.
    bool readyToMove();
    // The nozzle Offset Wizard's two steps: store where the nozzle left its
    // mark; then, the camera over the mark, move the nozzle's offset by the
    // difference (a step in Machine Setup, to undo).
    void nozzleOffsetWizard(const std::string& nozzleId, bool storeMark);

    struct Dock {
        std::unique_ptr<JDockWidget> dock;
        std::unique_ptr<JContainer>  panel;
    };

    JAppWindow&                         m_window;
    JSceneGraph&                        m_graph;
    JPlacerLayout                       m_layout;
    std::vector<JPFirmwareProfile>      m_profiles;
    std::unique_ptr<JPCell>             m_cell;
    std::string                         m_cellPath;
    std::vector<Dock>                   m_docks;
    struct CameraDock {
        std::unique_ptr<JDockWidget>   dock;
        std::unique_ptr<JPCameraPanel> panel;
    };
    std::vector<CameraDock>             m_cameras;   // the window's centre
    std::unique_ptr<JPlacerCameraTasks> m_cameraTasks;   // its Calibrate and Visual Test
    std::unique_ptr<JPlacerBoard>       m_board;         // the board on the machine, and its panel
    JPJogPanel*                         m_jog = nullptr; // its chosen tool, for the status bar
    JPMachineSetupPanel*                m_setup = nullptr;
    std::vector<std::function<void()>>  m_unwatch;   // this class's watches on the cell
    std::shared_ptr<bool>               m_alive = std::make_shared<bool>(true);
    JMenuItem*                          m_connectItem    = nullptr;
    JMenuItem*                          m_disconnectItem = nullptr;
    JMenuItem*                          m_homeItem       = nullptr;
    JMenuItem*                          m_parkItem       = nullptr;
    JMenuItem*                          m_undoItem       = nullptr;
    JMenuItem*                          m_redoItem       = nullptr;
    JPConnectIcon                       m_connectIcon;
    JPHomeIcon                          m_homeIcon;
    JPPositionReadout                   m_position;   // the chosen tool's, in the status bar
    bool                                m_connecting  = false;   // asked, not yet answered
    bool                                m_connectFailed = false; // the last connect failed
    std::string                         m_lost;                  // why the link dropped, until the next connect
    bool                                m_homeFailed = false;
    std::string                         m_setupSelected;   // Machine Setup's node, kept while the cell reopens
    struct NozzleMark {
        std::string nozzleId;
        double      x, y;
    };
    std::optional<NozzleMark>           m_nozzleMark;      // the Offset Wizard's stored mark
};

} // inline namespace jf
