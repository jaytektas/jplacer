// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerLayout.h"
#include <j/config/Settings.h>
#include "JPlacerSettings.h"

#include <algorithm>
#include <cstdio>
#include <set>
#include <sstream>

inline namespace jf {

namespace {

// The window's first shape, as shares of its size: the left column (cameras
// over the machine controls) and the console along the bottom.
constexpr float kLeftShare   = 0.3f;
constexpr float kBottomShare = 0.15f;
// Of the left column, the cameras' share over the machine controls (the
// Jog panel needs most of its height for its pad).
constexpr float kCameraShare = 0.38f;
// OpenPnP's Multiple Window Style: the cameras' window and the machine
// controls' window, where they first open, from the main window's corner
// (screen pixels), and how big.
constexpr int      kOwnWindowX = 60, kOwnWindowY = 80, kOwnWindowGap = 20;
constexpr uint32_t kCameraWindowW = 640, kCameraWindowH = 560;
constexpr uint32_t kControlsWindowW = 360, kControlsWindowH = 560;

} // namespace

JPlacerLayout::JPlacerLayout(JAppWindow& window)
    : m_window(window), m_ownWindows(JSettings::instance().get<bool>(JPlacerSettings::kMultipleWindows, false)) {
    JDockSpace& space = window.dockSpace();
    space.setCentreDocks(true);
    space.setSidesOwnCorners(false);   // the console runs the whole width
    space.setLeftWidth(float(window.width()) * kLeftShare);
    space.setBottomHeight(float(window.height()) * kBottomShare);
    // A dock the person closes (here or in a window of its own) is unticked.
    window.onDockClosed = [this](JDockWidget* dock) {
        if (Entry* e = find(dock); e && e->item) e->item->setChecked(false);
    };
}

JPlacerLayout::~JPlacerLayout() {
    m_window.onDockClosed = nullptr;
}

void JPlacerLayout::setViewMenu(JMenu* view, JSceneGraph& graph, std::function<void(JMenu&)> head) {
    m_view = view;
    m_viewHead = std::move(head);
    m_graph = &graph;
    rebuildMenu();
}

void JPlacerLayout::add(JDockWidget* dock, Home home, bool shown) {
    // Listed beside the others of its home (the cameras together, first).
    const auto at = std::find_if(m_entries.begin(), m_entries.end(), [home](const Entry& e) { return e.home > home; });
    const Entry& e = *m_entries.insert(at, Entry{ dock, home });
    if (shown) place(e);
    rebuildMenu();
}

void JPlacerLayout::remove(JDockWidget* dock) {
    if (JDockHost* host = dock->placedIn()) host->removeDock(dock);
    std::erase_if(m_entries, [dock](const Entry& e) { return e.dock == dock; });
    rebuildMenu();
}

void JPlacerLayout::show(JDockWidget* dock) {
    Entry* e = find(dock);
    if (!e) return;
    if (!dock->placedIn()) place(*e);
    // Re-inserting a dock where it already is makes it the active tab.
    if (JDockHost* host = dock->placedIn()) host->insertDock(dock, host->findDock(dock));
    if (e->item) e->item->setChecked(true);
}

JDockHost& JPlacerLayout::hostOf(Home home) {
    JDockSpace& space = m_window.dockSpace();
    switch (home) {
        case Home::Cameras:
        case Home::Controls: return space.left();
        case Home::Work:     return space.host(JDockSpace::Center);
        case Home::Console:  return space.bottom();
    }
    return space.host(JDockSpace::Center);
}

int JPlacerLayout::tabRank(const std::string& title) {
    // The work area's tabs in OpenPnP's order; jplacer's own after them.
    static const char* const kOrder[] = { "Job", "Panels", "Boards", "Parts", "Packages", "Vision", "Feeders",
                                          "Machine Setup", "Issues & Solutions", "Log" };
    for (size_t i = 0; i < std::size(kOrder); ++i)
        if (title == kOrder[i]) return int(i);
    return int(std::size(kOrder));
}

void JPlacerLayout::place(const Entry& e) {
    JDockHost& host = hostOf(e.home);
    // OpenPnP's Multiple Window Style: the cameras, and the machine controls, each in a window of
    // their own; tabbed with one of their home there already, else the first opening it.
    if (m_ownWindows && (e.home == Home::Cameras || e.home == Home::Controls)) {
        for (const Entry& o : m_entries)
            if (o.dock != e.dock && o.home == e.home && o.dock->placedIn() && o.dock->placedIn() != &host) {
                JDockHost* own = o.dock->placedIn();
                own->insertDock(e.dock, own->findDock(o.dock));
                return;
            }
        host.addDock(e.dock);
        const bool cameras = e.home == Home::Cameras;
        m_window.floatDockAt(&host, e.dock, m_window.windowX() + kOwnWindowX + (cameras ? 0 : int(kCameraWindowW) + kOwnWindowGap),
                             m_window.windowY() + kOwnWindowY, cameras ? kCameraWindowW : kControlsWindowW,
                             cameras ? kCameraWindowH : kControlsWindowH);
        return;
    }
    // Tabbed with one from the same home already there, in OpenPnP's order of tabs.
    for (const Entry& o : m_entries)
        if (o.dock != e.dock && o.home == e.home && o.dock->placedIn() == &host) {
            const JDockNodeId leaf = host.findDock(o.dock);
            int at = 0;
            if (const JDockNode* n = host.node(leaf))
                for (const JDockWidget* t : n->tabs)
                    if (tabRank(t->title()) <= tabRank(e.dock->title())) ++at;
            host.insertDock(e.dock, leaf, at);
            return;
        }
    // The host shared with another home (the cameras, the controls): above
    // or below what is there.
    for (const Entry& o : m_entries)
        if (o.dock != e.dock && o.dock->placedIn() == &host) {
            const JDockNodeId leaf = host.splitLeaf(host.findDock(o.dock),
                                                    e.home == Home::Cameras ? JDropPos::Top : JDropPos::Bottom);
            if (leaf.valid() && host.insertDock(e.dock, leaf)) {
                // The cameras above (the split's first), the controls below.
                if (JDockNode* n = host.node(leaf); n && n->parent.valid())
                    if (JDockNode* split = host.node(n->parent); split && split->weights.size() == 2)
                        split->weights = { kCameraShare, 1 - kCameraShare };
                return;
            }
        }
    host.addDock(e.dock);
}

void JPlacerLayout::hide(const Entry& e) {
    if (JDockHost* host = e.dock->placedIn()) host->removeDock(e.dock);
}

void JPlacerLayout::rebuildMenu() {
    if (!m_view || !m_graph) return;
    m_view->clear();
    if (m_viewHead) {
        m_viewHead(*m_view);
        m_view->addSeparator(*m_graph);
    }
    for (size_t i = 0; i < m_entries.size(); ++i) {
        Entry& e = m_entries[i];
        if (i > 0 && m_entries[i - 1].home != e.home) m_view->addSeparator(*m_graph);
        e.item = m_view->add(*m_graph, e.dock->title());
        e.item->setCheckable(true);
        e.item->setChecked(e.dock->placedIn() != nullptr);
        // The tick has already flipped when this runs: it says what is wanted.
        e.item->onTriggered.connect([this, dock = e.dock] {
            Entry* entry = find(dock);
            if (!entry) return;
            if (entry->item->isChecked()) show(dock);
            else hide(*entry);
        });
    }
}

void JPlacerLayout::save() const {
    JSettings& set = JSettings::instance();
    JPlatformWindow& w = m_window.window();
    char geometry[96];
    std::snprintf(geometry, sizeof geometry, "%d %d %u %u %d", w.screenX(), w.screenY(), w.width(), w.height(),
                  w.isMaximized() ? 1 : 0);
    set.set(JPlacerSettings::kWindowGeometry, std::string(geometry));
    set.set(JPlacerSettings::kDockLayout, m_window.dockLayoutText());
    std::string closed;
    for (const Entry& e : m_entries)
        if (!e.dock->placedIn()) closed += e.dock->title() + "\n";
    set.set(JPlacerSettings::kClosedDocks, closed);
    JPlacerSettings::save();
}

void JPlacerLayout::restore() {
    JSettings& set = JSettings::instance();
    JPlatformWindow& w = m_window.window();
    int x = 0, y = 0, maximized = 0;
    unsigned width = 0, height = 0;
    if (std::sscanf(set.get<std::string>(JPlacerSettings::kWindowGeometry, "").c_str(), "%d %d %u %u %d", &x, &y, &width,
                    &height, &maximized) == 5) {
        if (maximized) w.setMaximized(true);
        else if (width > 0 && height > 0) {
            w.setPosition(x, y);
            w.setSize(width, height);
        }
    }
    const std::string text = set.get<std::string>(JPlacerSettings::kDockLayout, "");
    if (text.empty() || !m_window.restoreDockLayout(text)) return;
    // Closed when it was left: closed again (not put back at its home).
    std::set<std::string> closed;
    std::istringstream lines(set.get<std::string>(JPlacerSettings::kClosedDocks, ""));
    for (std::string t; std::getline(lines, t);) closed.insert(t);
    for (const Entry& e : m_entries)
        if (closed.count(e.dock->title())) hide(e);
    rebuildMenu();
}

JPlacerLayout::Entry* JPlacerLayout::find(const JDockWidget* dock) {
    for (Entry& e : m_entries)
        if (e.dock == dock) return &e;
    return nullptr;
}

} // inline namespace jf
