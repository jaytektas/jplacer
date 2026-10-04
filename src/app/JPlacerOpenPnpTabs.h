// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPlacerJob.h"
#include "JPlacerLayout.h"

#include "ui/JPPartsPanel.h"

#include <j/app/JAppWindow.h>
#include <j/core/DockWidget.h>

#include <memory>
#include <string>

inline namespace jf {

// OpenPnP's tabs in the work area (Parts so far), each a dock, kept in step
// with the job and its configuration: an edit on one is saved and the
// others shown again.
class JPlacerOpenPnpTabs {
public:
    JPlacerOpenPnpTabs(JAppWindow& window, JSceneGraph& graph, JPlacerJob& job, JPlacerLayout& layout);
    ~JPlacerOpenPnpTabs();

    // Brings a tab forward by its title; false when there is none.
    bool showDock(const std::string& title);

private:
    JAppWindow&                   m_window;
    JPlacerJob&                   m_job;
    JPlacerLayout&                m_layout;
    int                           m_watch = 0;
    std::unique_ptr<JPPartsPanel> m_parts;
    std::unique_ptr<JDockWidget>  m_partsDock;
};

} // inline namespace jf
