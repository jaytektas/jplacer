// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerViewerDock.h"

#include "model/JPBoardLocation.h"
#include "model/JPPanelLocation.h"

inline namespace jf {

JPlacerViewerDock::JPlacerViewerDock(JSceneGraph& graph, JPlacerLayout& layout, std::string kind)
    : m_layout(layout), m_kind(std::move(kind)),
      m_viewer(std::make_unique<JPPlacementsViewer>(graph, nullptr, m_kind == "Job")) {}

JPlacerViewerDock::~JPlacerViewerDock() {
    if (m_dock) {
        m_layout.remove(m_dock.get());
        m_dock->setContent(nullptr);
    }
}

void JPlacerViewerDock::show(std::shared_ptr<JPPlacementsHolder> holder) {
    if (holder->kind() == JPPlacementsHolder::Kind::Board) {
        m_root = std::make_unique<JPBoardLocation>();
    } else {
        m_root = std::make_unique<JPPanelLocation>();
    }
    m_root->holder = std::move(holder);
    if (m_root->kind() == JPPlacementsHolderLocation::Kind::Panel)
        static_cast<JPPanelLocation*>(m_root.get())->setParentsOfAllDescendants();
    m_viewer->setRoot(m_root.get());
    place();
    // A dock's title names it for the layout, so the board's name is shown within.
    m_viewer->setName(m_root->holder->name.value_or(""));
}

void JPlacerViewerDock::place() {
    if (m_dock) return;
    m_dock = std::make_unique<JDockWidget>(m_kind + " Viewer", 0.f, 0.f, 0.f, 0.f);
    m_dock->setContent(m_viewer.get());
    m_layout.add(m_dock.get(), JPlacerLayout::Home::Cameras);
}

void JPlacerViewerDock::showJob(JPPanelLocation* root, const std::string& name,
                                std::vector<const JPPlacementsHolderLocation*> chosen) {
    m_jobRoot = root;
    m_viewer->canvas().setSelections(std::move(chosen));
    m_viewer->setRoot(root);
    m_viewer->setName(name);
    place();
}

void JPlacerViewerDock::openJob(JPPanelLocation* root, std::string name, std::vector<const JPPlacementsHolderLocation*> chosen) {
    if (!root) return;
    showJob(root, name, std::move(chosen));
    m_open = true;
    m_layout.show(m_dock.get());
}

void JPlacerViewerDock::followJob(JPPanelLocation* root, std::string name, std::vector<const JPPlacementsHolderLocation*> chosen) {
    if (m_open && root) showJob(root, name, std::move(chosen));
}

void JPlacerViewerDock::open(std::shared_ptr<JPPlacementsHolder> holder) {
    if (!holder) return;
    show(std::move(holder));
    m_open = true;
    m_layout.show(m_dock.get());
}

void JPlacerViewerDock::follow(std::shared_ptr<JPPlacementsHolder> holder) {
    if (m_open && holder && (!m_root || holder != m_root->holder)) show(std::move(holder));
}

void JPlacerViewerDock::regenerate() {
    if (m_jobRoot) {
        m_jobRoot->setParentsOfAllDescendants();
        m_viewer->regenerate();
        return;
    }
    if (!m_root) return;
    if (m_root->kind() == JPPlacementsHolderLocation::Kind::Panel)
        static_cast<JPPanelLocation*>(m_root.get())->setParentsOfAllDescendants();
    m_viewer->regenerate();
}

} // inline namespace jf
