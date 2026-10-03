// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerLayout.h"

#include <algorithm>

inline namespace jf {

namespace {

// The window's first shape, as shares of its size: the left column (cameras
// over the machine controls) and the console along the bottom.
constexpr float kLeftShare   = 0.3f;
constexpr float kBottomShare = 0.2f;

} // namespace

JPlacerLayout::JPlacerLayout(JAppWindow& window) : m_window(window) {
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

void JPlacerLayout::setViewMenu(JMenu* view, JSceneGraph& graph) {
    m_view = view;
    m_graph = &graph;
    rebuildMenu();
}

void JPlacerLayout::add(JDockWidget* dock, Home home) {
    // Listed beside the others of its home (the cameras together, first).
    const auto at = std::find_if(m_entries.begin(), m_entries.end(), [home](const Entry& e) { return e.home > home; });
    const Entry& e = *m_entries.insert(at, Entry{ dock, home });
    place(e);
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

void JPlacerLayout::place(const Entry& e) {
    JDockHost& host = hostOf(e.home);
    // Tabbed with one from the same home already there.
    for (const Entry& o : m_entries)
        if (o.dock != e.dock && o.home == e.home && o.dock->placedIn() == &host) {
            host.insertDock(e.dock, host.findDock(o.dock));
            return;
        }
    // The host shared with another home (the cameras, the controls): above
    // or below what is there.
    for (const Entry& o : m_entries)
        if (o.dock != e.dock && o.dock->placedIn() == &host) {
            const JDockNodeId leaf = host.splitLeaf(host.findDock(o.dock),
                                                    e.home == Home::Cameras ? JDropPos::Top : JDropPos::Bottom);
            if (leaf.valid() && host.insertDock(e.dock, leaf)) return;
        }
    host.addDock(e.dock);
}

void JPlacerLayout::hide(const Entry& e) {
    if (JDockHost* host = e.dock->placedIn()) host->removeDock(e.dock);
}

void JPlacerLayout::rebuildMenu() {
    if (!m_view || !m_graph) return;
    m_view->clear();
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

JPlacerLayout::Entry* JPlacerLayout::find(const JDockWidget* dock) {
    for (Entry& e : m_entries)
        if (e.dock == dock) return &e;
    return nullptr;
}

} // inline namespace jf
