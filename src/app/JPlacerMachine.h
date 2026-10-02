// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "machine/JPCell.h"
#include "ui/JPMachinePanel.h"

#include <j/app/JAppWindow.h>
#include <j/core/DockWidget.h>
#include <j/core/MenuSystem.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

inline namespace jf {

// The machine jplacer is working with: the open cell (cells/<name>.json),
// its panel in a dock, and the Machine menu's actions on it.
//
// The cell opened last is opened again at start (JPlacerSettings::kMachineCell).
class JPlacerMachine {
public:
    JPlacerMachine(JAppWindow& window, JSceneGraph& graph);
    ~JPlacerMachine();

    JPlacerMachine(const JPlacerMachine&)            = delete;
    JPlacerMachine& operator=(const JPlacerMachine&) = delete;

    // The Machine menu's entries this class enables and disables.
    void setMenuItems(JMenuItem* connect, JMenuItem* disconnect);

    void chooseCell();          // Machine > Open Cell…
    void importOpenPnp();       // Machine > Import OpenPnP Machine…
    void connect();
    void disconnect();

    // The directory cell files are kept in.
    static std::string cellsDir();

private:
    bool openCell(const std::string& path, std::string& error);
    void updateMenu();

    JAppWindow&                         m_window;
    JSceneGraph&                        m_graph;
    std::vector<JPFirmwareProfile>      m_profiles;
    std::unique_ptr<JPCell>             m_cell;
    std::unique_ptr<JDockWidget>        m_dock;
    std::unique_ptr<JPMachinePanel>     m_panel;
    std::function<void()>               m_unwatch;   // the menu's watch on the cell's connection
    std::shared_ptr<bool>               m_alive = std::make_shared<bool>(true);
    JMenuItem*                          m_connectItem    = nullptr;
    JMenuItem*                          m_disconnectItem = nullptr;
};

} // inline namespace jf
