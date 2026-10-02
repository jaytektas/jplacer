// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "machine/JPCell.h"

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
// its panels, each in a dock of its own (Machine, Jog, Actuators, Console,
// Axes), the strip across the window saying what state it is in, and the
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
    void setMenuItems(JMenuItem* connect, JMenuItem* disconnect, JMenuItem* home);

    void chooseCell();          // Machine > Open Cell…
    // Machine > Import OpenPnP Machine…: offers OpenPnP's usual machine.xml
    // when there is one, else (or on request) a file to choose.
    void importOpenPnp();
    void connect();
    void disconnect();
    void home();                // Machine > Home All Axes

    // The directory cell files are kept in.
    static std::string cellsDir();

private:
    bool openCell(const std::string& path, std::string& error);
    void importFrom(const std::string& machineXml);
    void setPort(const std::string& driverId, const std::string& port);
    void updateMenu();
    // The strip in the window's chrome: NOT CONNECTED (with `why`, the last
    // failure), ALARM, NOT HOMED, or nothing when the machine is ready.
    void showNotice(const std::string& why);
    // One dock per panel, made the first time a cell opens; a new cell gets
    // new panels in the same docks, so where the person put them is kept.
    void buildPanels();
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
    std::vector<std::function<void()>>  m_unwatch;   // this class's watches on the cell
    std::shared_ptr<bool>               m_alive = std::make_shared<bool>(true);
    JMenuItem*                          m_connectItem    = nullptr;
    JMenuItem*                          m_disconnectItem = nullptr;
    JMenuItem*                          m_homeItem       = nullptr;
};

} // inline namespace jf
