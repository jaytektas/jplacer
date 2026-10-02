// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPlacerBoard.h"
#include "JPlacerCameraTasks.h"

#include "machine/JPCell.h"
#include "ui/JPCameraPanel.h"
#include "ui/JPConnectIcon.h"
#include "ui/JPHomeIcon.h"

#include <j/app/JAppWindow.h>
#include <j/core/DockWidget.h>
#include <j/core/JContainer.h>
#include <j/core/MenuSystem.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

inline namespace jf {

// The machine jplacer is working with: the open cell (cells/<name>.json),
// its panels, each in a dock of its own (a camera each, tabbed together in
// the window's centre; Machine, Jog, Actuators, Board, Console, Axes), the strip across the window saying what state it is in, and the
// Machine menu's actions on it.
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

    void chooseCell();          // Machine > Open Cell…
    // Machine > Import OpenPnP Machine…: offers OpenPnP's usual machine.xml
    // when there is one, else (or on request) a file to choose.
    void importOpenPnp();
    void connect();
    void disconnect();
    void home();                // Machine > Home All Axes
    void park();                // Machine > Park Head
    // Bring the dock titled `title` to the front of its tab group. False when
    // there is no such dock.
    bool showDock(const std::string& title);

    // The directory cell files are kept in.
    static std::string cellsDir();

private:
    bool openCell(const std::string& path, std::string& error);
    void importFrom(const std::string& machineXml);
    void setPort(const std::string& driverId, const std::string& port);
    // Correct the squareness of the gantry moving `mount` by `xPerY` more (from
    // a board), and keep it in the cell file.
    void squareMachine(const JPMountConfig& mount, double xPerY);
    void updateMenu();
    // The connect and home icons follow the cell; the strip across the window
    // is kept for what is critical (ALARM, CONNECTION LOST) and a failure goes
    // to the status bar.
    void showState();
    // One dock per panel, made the first time a cell opens; a new cell gets
    // new panels in the same docks, so where the person put them is kept.
    // The cameras' docks are the cell's own, made new with it.
    void buildPanels();
    void buildCameras();
    // A camera's light is on while its camera runs (on screen) and the
    // machine is connected; while not connected, a camera with a light says
    // why its picture is dark.
    void lightCameras();
    void bringForward(JPCameraPanel& camera);
    void dropPanels();

    struct Dock {
        std::unique_ptr<JDockWidget> dock;
        std::unique_ptr<JContainer>  panel;
    };

    JAppWindow&                         m_window;
    JSceneGraph&                        m_graph;
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
    std::vector<std::function<void()>>  m_unwatch;   // this class's watches on the cell
    std::shared_ptr<bool>               m_alive = std::make_shared<bool>(true);
    JMenuItem*                          m_connectItem    = nullptr;
    JMenuItem*                          m_disconnectItem = nullptr;
    JMenuItem*                          m_homeItem       = nullptr;
    JMenuItem*                          m_parkItem       = nullptr;
    JPConnectIcon                       m_connectIcon;
    JPHomeIcon                          m_homeIcon;
    bool                                m_connecting  = false;   // asked, not yet answered
    bool                                m_connectFailed = false; // the last connect failed
    std::string                         m_lost;                  // why the link dropped, until the next connect
    bool                                m_homeFailed = false;
};

} // inline namespace jf
