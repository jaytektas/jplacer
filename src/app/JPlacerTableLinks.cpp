// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerTableLinks.h"

#include "model/JPBoard.h"
#include "model/JPPanel.h"
#include "ui/JPFeedersPanel.h"
#include "ui/JPJobPanel.h"
#include "ui/JPPackagesPanel.h"
#include "ui/JPPartsPanel.h"
#include "ui/JPVisionSettingsPanel.h"

#include <j/core/DockManager.h>

#include <utility>

inline namespace jf {

JPlacerTableLinks::JPlacerTableLinks(Tabs tabs, JPConfiguration& config, std::function<bool()> linked)
    : m_tabs(tabs), m_config(config), m_linked(std::move(linked)) {
    m_tabs.job.placements().onPlacementChosen = [this](const JPPlacement* p) {
        if (leads(m_tabs.jobDock)) choose([&] { jobPlacementChosen(p); });
    };
    m_tabs.job.board().onPlacementChosen = [this](const JPPlacement* p) {
        if (leads(m_tabs.jobDock)) choose([&] { boardPlacementChosen(p); });
    };
    m_tabs.job.panel().onFiducialChosen = [this](const JPPlacement* f) {
        if (f && leads(m_tabs.jobDock)) choose([&] { panelFiducialChosen(*f); });
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

void JPlacerTableLinks::jobPlacementChosen(const JPPlacement* p) {
    const JPPlacementsHolderLocation* l = m_tabs.job.placements().location();
    if (!l || !l->holder) return;
    if (l->kind() == JPPlacementsHolderLocation::Kind::Board) m_tabs.job.board().selectPlacement(p ? p->id : "");
    else m_tabs.job.panel().selectFiducial(p ? p->id : "");
    if (p) partAndLinks(p->partId, true);
}

void JPlacerTableLinks::boardPlacementChosen(const JPPlacement* p) {
    if (!p) return;
    // The same placement in the job's, as it shows (an instance of) this board.
    const JPPlacementsHolderLocation* l = m_tabs.job.placements().location();
    const JPBoard* board = m_tabs.job.board().board();
    if (l && l->holder && board && l->holder->definition() == board) m_tabs.job.placements().selectPlacement(p->id);
    partAndLinks(p->partId, true);
}

void JPlacerTableLinks::panelFiducialChosen(const JPPlacement& f) {
    const JPPlacementsHolderLocation* l = m_tabs.job.placements().location();
    const JPPanel* panel = m_tabs.job.panel().panel();
    if (l && l->holder && panel && l->holder->definition() == panel) m_tabs.job.placements().selectPlacement(f.id);
}

} // inline namespace jf
