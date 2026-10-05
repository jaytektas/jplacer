// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerTableLinks.h"

#include "model/JPBoard.h"
#include "model/JPPanel.h"
#include "ui/JPBoardsPanel.h"
#include "ui/JPFeedersPanel.h"
#include "ui/JPJobPanel.h"
#include "ui/JPPackagesPanel.h"
#include "ui/JPPanelsPanel.h"
#include "ui/JPPartsPanel.h"
#include "ui/JPVisionSettingsPanel.h"

#include <j/core/DockManager.h>

#include <utility>

inline namespace jf {

JPlacerTableLinks::JPlacerTableLinks(Tabs tabs, JPConfiguration& config, std::function<bool()> linked)
    : m_tabs(tabs), m_config(config), m_linked(std::move(linked)) {
    m_tabs.job.onLocationChosen = [this](const JPPlacementsHolderLocation& l) {
        if (leads(m_tabs.jobDock)) choose([&] { jobLocationChosen(l); });
    };
    m_tabs.job.placements().onPlacementChosen = [this](const JPPlacement* p) {
        if (leads(m_tabs.jobDock)) choose([&] { jobPlacementChosen(p); });
    };
    m_tabs.boards.placements().onPlacementChosen = [this](const JPPlacement* p) {
        if (leads(m_tabs.boardsDock)) choose([&] { boardPlacementChosen(p); });
    };
    m_tabs.panels.definition().onChildChosen = [this](const JPPlacementsHolderLocation* child) {
        if (child && leads(m_tabs.panelsDock)) choose([&] { panelChildChosen(*child); });
    };
    m_tabs.panels.definition().onFiducialChosen = [this](const JPPlacement* f) {
        if (f && leads(m_tabs.panelsDock)) choose([&] { panelFiducialChosen(*f); });
    };
    m_tabs.parts.onPartChosen = [this](const JPPart& part) {
        if (leads(m_tabs.partsDock)) choose([&] { partAndLinks(part.id, false); });
    };
    m_tabs.feeders.onFeederChosen = [this](const JPFeeder& f) {
        if (!f.partId().empty() && leads(m_tabs.feedersDock)) choose([&] { partAndLinks(f.partId(), true); });
    };
}

bool JPlacerTableLinks::leads(const JDockWidget& dock) const {
    if (m_linking || !m_linked()) return false;
    const JDockHost* host = dock.placedIn();
    return host && host->tabStateOf(&dock).activeTab;
}

void JPlacerTableLinks::choose(const std::function<void()>& linkedChoices) {
    m_linking = true;
    linkedChoices();
    m_linking = false;
}

void JPlacerTableLinks::partAndLinks(const std::string& partId, bool choosePart) {
    const JPPart* part = m_config.part(partId);
    if (choosePart) m_tabs.parts.selectPart(part);
    if (!part) return;
    m_tabs.packages.selectPackage(m_config.package(part->packageId));
    m_tabs.feeders.selectFeederForPart(part->id);
    m_tabs.vision.selectFor(*part);
}

void JPlacerTableLinks::jobLocationChosen(const JPPlacementsHolderLocation& l) {
    const JPPlacementsHolder* definition = l.holder ? l.holder->definition() : nullptr;
    if (l.kind() == JPPlacementsHolderLocation::Kind::Board) {
        m_tabs.boards.selectBoard(static_cast<const JPBoard*>(definition));
        m_tabs.boards.placements().selectPlacement("");
        // A board in a panel: its panel, and the board in it.
        if (l.parent && l.parent->parent && l.parent->holder) {
            m_tabs.panels.selectPanel(static_cast<const JPPanel*>(l.parent->holder->definition()));
            m_tabs.panels.definition().selectChild(l);
        }
        return;
    }
    m_tabs.panels.selectPanel(static_cast<const JPPanel*>(definition));
    m_tabs.panels.definition().selectFiducial("");
}

void JPlacerTableLinks::jobPlacementChosen(const JPPlacement* p) {
    const JPPlacementsHolderLocation* l = m_tabs.job.placements().location();
    if (!l || !l->holder) return;
    if (l->kind() == JPPlacementsHolderLocation::Kind::Board) {
        m_tabs.boards.placements().selectPlacement(p ? p->id : "");
    } else {
        m_tabs.panels.selectPanel(static_cast<const JPPanel*>(l->holder->definition()));
        m_tabs.panels.definition().selectFiducial(p ? p->id : "");
    }
    if (p) partAndLinks(p->partId, true);
}

void JPlacerTableLinks::boardPlacementChosen(const JPPlacement* p) {
    if (!p) return;
    // The same placement in the Job tab's, when it shows (an instance of) this board.
    const JPPlacementsHolderLocation* l = m_tabs.job.placements().location();
    const JPBoard* board = m_tabs.boards.placements().board();
    if (l && l->holder && board && l->holder->definition() == board) m_tabs.job.placements().selectPlacement(p->id);
    partAndLinks(p->partId, true);
}

void JPlacerTableLinks::panelChildChosen(const JPPlacementsHolderLocation& child) {
    m_tabs.job.selectLocation(&child);
    if (child.kind() == JPPlacementsHolderLocation::Kind::Board && child.holder) {
        m_tabs.boards.selectBoard(static_cast<const JPBoard*>(child.holder->definition()));
        m_tabs.boards.placements().selectPlacement("");
    }
}

void JPlacerTableLinks::panelFiducialChosen(const JPPlacement& f) {
    const JPPlacementsHolderLocation* l = m_tabs.job.placements().location();
    const JPPanel* panel = m_tabs.panels.definition().panel();
    if (l && l->holder && panel && l->holder->definition() == panel) m_tabs.job.placements().selectPlacement(f.id);
}

} // inline namespace jf
