// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPlacerJob.h"
#include "JPlacerLayout.h"

#include "library/JPEntry.h"
#include "ui/JPFootprintView.h"
#include "ui/JPPartsPanel.h"

#include <j/core/DockWidget.h>

#include <memory>

inline namespace jf {

// The Parts dock over the open job: each placement with its part, package,
// footprint, rotation and state (JPPlacementState), and pages to edit the
// chosen placements and their part, package and footprint:
//
//  - Placement: its rotation (typed once for every placement chosen, or put
//    back to its part's), its part (another chosen, a guess confirmed, or a
//    new one made from what the files said);
//  - Part, Package, Footprint: their fields; a part's package and a
//    package's footprint chosen from the job's or brought from the library;
//    a footprint imported from KiCad or made from numbers; Copy to Library
//    (at once when new, after showing the differences when it would replace
//    the library's), Update from Library and Keep This Version when the
//    library's has changed; Remove.
//
// It is the job's, so it is there whether or not a machine is open. Its list
// or tree, and the tree's grouping, are kept in the settings
// (JPlacerSettings::kParts...).
class JPlacerParts {
public:
    static constexpr const char* kTitle = "Parts";

    JPlacerParts(JPlacerJob& job, JSceneGraph& graph, JPlacerLayout& layout);
    ~JPlacerParts();

    // Shown, and brought to the front of its tabs.
    void showDock();

private:
    void show();
    void pages();
    void placementPage();
    void entryPage(size_t page, const JPEntry& e);
    std::vector<JPPlacement*> chosen();
    JPEntry entryOn(size_t page);
    void onPlacementField(const std::string& key, const std::string& text);
    void onPlacementChoice(const std::string& key, int index);
    void onPlacementAction(const std::string& key);
    void onEntryField(size_t page, const std::string& key, const std::string& text);
    void onEntryChoice(size_t page, const std::string& key, int index);
    void onEntryAction(size_t page, const std::string& key);
    void copyToLibrary(const JPEntry& e);
    void edited(JPlacerJob::Change what = JPlacerJob::Change::Parts);
    void say(const std::string& text);

    JPlacerJob&                   m_job;
    JPlacerLayout&                m_layout;
    int                           m_watch = 0;
    std::unique_ptr<JPPartsPanel> m_panel;
    std::unique_ptr<JDockWidget>  m_dock;
    JPFootprintView*              m_footprintView = nullptr;
    std::vector<std::string>      m_chosen;
    // What each choice list offers, in its order: a job entry's id, or a
    // library entry's id to bring in ("lib:" + id), or empty for none.
    std::vector<std::string>      m_partChoices, m_packageChoices, m_footprintChoices;
    std::shared_ptr<bool>         m_alive = std::make_shared<bool>(true);
};

} // inline namespace jf
