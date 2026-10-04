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

// The Library dock: the main library's parts, packages and footprints, in
// the same list or tree as the Parts dock, each edited on its page and
// saved as it is changed (not at all while the library is read-only, its
// file not readable). Bring into Job copies the chosen entry into the open
// job; New Part and New Package start one; a package's footprint is chosen,
// imported from KiCad or made from numbers; Remove takes one out.
class JPlacerLibraryDock {
public:
    static constexpr const char* kTitle = "Library";

    JPlacerLibraryDock(JPlacerJob& job, JSceneGraph& graph, JPlacerLayout& layout);
    ~JPlacerLibraryDock();

    void showDock();

private:
    void show();
    void pages();
    JPEntry chosenEntry() const;
    void onField(const JPEntry& e, const std::string& key, const std::string& text);
    void onChoice(const JPEntry& e, const std::string& key, int index);
    void onAction(const JPEntry& e, const std::string& key);
    void changed();
    void say(const std::string& text);

    JPlacerJob&                   m_job;
    JPlacerLayout&                m_layout;
    int                           m_watch = 0;
    std::unique_ptr<JPPartsPanel> m_panel;
    std::unique_ptr<JDockWidget>  m_dock;
    JPFootprintView*              m_footprintView = nullptr;
    std::string                   m_chosen;   // its row's key: "<kind>:<id>"
    std::vector<std::string>      m_packageChoices, m_footprintChoices;
    std::shared_ptr<bool>         m_alive = std::make_shared<bool>(true);
};

} // inline namespace jf
