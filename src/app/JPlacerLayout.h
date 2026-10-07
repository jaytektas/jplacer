// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <j/app/JAppWindow.h>
#include <j/core/DockWidget.h>
#include <j/core/MenuSystem.h>

#include <functional>
#include <string>
#include <vector>

inline namespace jf {

// Where jplacer's docks live, and the View menu that shows and hides them.
//
//   +-----------+---------------------------------+
//   | cameras   |                                 |
//   | (tabbed)  |   work: Board, Machine Setup,   |
//   +-----------+   Machine… (tabbed)             |
//   | machine   |                                 |
//   | controls  |                                 |
//   +-----------+---------------------------------+
//   | console                                     |
//   +---------------------------------------------+
//
// Each dock has a home it is put in when it is first added and whenever it
// is shown again after being closed: beside the docks already there
// (tabbed), the cameras above the machine controls. Where the person moves
// one, it stays; every split can be dragged.
class JPlacerLayout {
public:
    enum class Home { Cameras, Controls, Work, Console };

    explicit JPlacerLayout(JAppWindow& window);
    ~JPlacerLayout();

    JPlacerLayout(const JPlacerLayout&)            = delete;
    JPlacerLayout& operator=(const JPlacerLayout&) = delete;

    // View's entries, a tick for each dock that is showing, in step with the
    // docks from now on; `head` adds the entries over them each time they are made.
    void setViewMenu(JMenu* view, JSceneGraph& graph, std::function<void(JMenu&)> head);

    // A dock to lay out: shown at its home now (unless not `shown`: closed, to be shown from View), and listed in View.
    void add(JDockWidget* dock, Home home, bool shown = true);
    // Before a dock goes: taken out of wherever it is, and off View.
    void remove(JDockWidget* dock);
    // Shown (at its home, if it was closed) and brought to the front of its tabs.
    void show(JDockWidget* dock);
    // The window as it is now kept for next time: its place and size, every dock where it is (docked, or in
    // a window of its own), and which are closed.
    void save() const;
    // The window as kept last time, if it was: once every dock is added. A dock new since then stays at
    // its home.
    void restore();

private:
    struct Entry {
        JDockWidget* dock;
        Home         home;
        JMenuItem*   item = nullptr;
    };

    JDockHost& hostOf(Home home);
    // Where a tab goes among those of its home: OpenPnP's tabs in its order,
    // the rest after them.
    static int tabRank(const std::string& title);
    void place(const Entry& e);
    void hide(const Entry& e);
    void rebuildMenu();
    Entry* find(const JDockWidget* dock);

    JAppWindow&        m_window;
    // OpenPnP's Multiple Window Style (JPlacerSettings::kMultipleWindows), taken at start.
    const bool         m_ownWindows;
    std::vector<Entry> m_entries;   // in the order View lists them
    JMenu*             m_view = nullptr;
    std::function<void(JMenu&)> m_viewHead;
    JSceneGraph*       m_graph = nullptr;
};

} // inline namespace jf
