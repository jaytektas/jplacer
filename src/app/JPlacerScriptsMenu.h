// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "machine/JPScripting.h"

#include <j/app/JAppWindow.h>
#include <j/core/MenuSystem.h>

#include <memory>
#include <vector>

inline namespace jf {

// OpenPnP's Scripts menu (its ScriptFileWatcher): the scripts folder's
// scripts, by name, a folder a submenu of its own (but the Events folder, and
// one holding a file named .ignore); then Refresh Scripts (the folder read
// again), Open Scripts Directory, and Clear Scripting Engine Pool, greyed:
// each script runs as a program of its own, so there is no pool to clear.
// A script chosen runs off the screen's thread; how it went is said in the
// status line.
class JPlacerScriptsMenu {
public:
    JPlacerScriptsMenu(JAppWindow& window, JSceneGraph& graph, std::shared_ptr<JPScripting> scripting, JMenu* menu);
    ~JPlacerScriptsMenu();
    void refresh();

private:
    void fill(JMenu* menu, const std::string& directory);
    void run(const std::string& path);

    JAppWindow&                         m_window;
    JSceneGraph&                        m_graph;
    std::shared_ptr<JPScripting>        m_scripting;
    JMenu*                              m_menu;
    std::vector<std::unique_ptr<JMenu>> m_subMenus;
    std::shared_ptr<bool>               m_alive = std::make_shared<bool>(true);
};

} // inline namespace jf
