// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPlacerJob.h"
#include "JPlacerLayout.h"

#include "ui/JPPartsPanel.h"

#include <j/core/DockWidget.h>

#include <memory>

inline namespace jf {

// The Parts dock over the open job: each placement with its part, package,
// footprint and state (JPPlacementState), and for the one chosen, what it
// has and what it lacks. It is the job's, so it is there whether or not a
// machine is open. Its list or tree, and the tree's grouping, are kept in
// the settings (JPlacerSettings::kParts...).
class JPlacerParts {
public:
    static constexpr const char* kTitle = "Parts";

    JPlacerParts(JPlacerJob& job, JSceneGraph& graph, JPlacerLayout& layout);
    ~JPlacerParts();

    // Shown, and brought to the front of its tabs.
    void showDock();

private:
    void show();
    void detail();

    JPlacerJob&                   m_job;
    JPlacerLayout&                m_layout;
    int                           m_watch = 0;
    std::unique_ptr<JPPartsPanel> m_panel;
    std::unique_ptr<JDockWidget>  m_dock;
    std::vector<std::string>      m_chosen;
};

} // inline namespace jf
