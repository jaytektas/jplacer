// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPGroupFrame.h"
#include "JPIconButton.h"
#include "JPJobPlacementsPanel.h"
#include "JPLocationsTableModel.h"
#include "JPTable.h"

#include "model/JPConfiguration.h"
#include "model/JPJob.h"

#include <j/core/JContainer.h>
#include <j/core/MenuSystem.h>
#include <j/core/Splitter.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

inline namespace jf {

// The Job tab, as OpenPnP's JobPanel: the job's Boards (its boards and
// panels, nested, where each lies, its side, enabled and fiducial check)
// over the chosen one's Placements (JPJobPlacementsPanel). The toolbar:
// Start, Step, Stop, Defer or Alert Errors, Add Board/Panel (a new or
// existing board or panel), Remove Board(s)/Panel(s), Move Camera To (and
// To the Next) Board Location, Move Tool To Board Location, Capture Camera
// and Tool Location, Multiple Point Board Location, Fiducial Check, View
// Job; the right-click Set Side, Set Enabled, Set Check Fids.
class JPJobPanel : public JContainer {
public:
    using Tool = JPJobPlacementsPanel::Tool;

    JPJobPanel(JSceneGraph& graph, JPConfiguration& config, std::function<JPJob*()> job, double split);

    // The job changed (to be shown as changed, other views told).
    std::function<void()> onChanged;
    std::function<void(JMenu*, float x, float y)> openMenu;
    std::function<std::optional<JPLocation>(Tool)> toolLocation;
    std::function<void(Tool, const JPLocation&)> moveTool;
    std::function<void(const std::string& title, const std::string& what, std::function<void(std::string)> chosen)>
        chooseExisting;
    // View Job, showing the job (and the boards and panels chosen).
    std::function<void(std::vector<const JPPlacementsHolderLocation*> chosen)> onViewJob;
    // The chosen boards and panels changed (the job viewer follows them).
    std::function<void(std::vector<const JPPlacementsHolderLocation*> chosen)> onSelectionChanged;

    JPJobPlacementsPanel& placements() { return *m_placements; }
    // Another job, or the job changed elsewhere: shown again.
    void refresh();
    double split() const;

private:
    std::vector<JPPlacementsHolderLocation*> selections() const;
    void selectionChanged();
    void updateJobActions();
    void buildMenu();
    void showAddMenu();
    void addBoard(const std::string& path, const char* errorTitle);
    void addPanel(const std::string& path, const char* errorTitle);
    void removeSelected();
    void moveTo(Tool tool, bool next);
    void captureCamera();
    void captureTool();
    void changed();

    JPConfiguration&                    m_config;
    std::function<JPJob*()>             m_job;
    JPLocationsTableModel               m_model;
    JPTable*                            m_table = nullptr;
    JPJobPlacementsPanel*               m_placements = nullptr;
    JSplitter*                          m_split = nullptr;
    std::unique_ptr<JContainer>         m_boardsPane, m_placementsPane;
    JPIconButton*                       m_start = nullptr;
    JPIconButton*                       m_step = nullptr;
    JPIconButton*                       m_stop = nullptr;
    JPIconButton*                       m_errors = nullptr;
    JPIconButton*                       m_add = nullptr;
    JPIconButton*                       m_remove = nullptr;
    JPIconButton*                       m_cameraTo = nullptr;
    JPIconButton*                       m_cameraNext = nullptr;
    JPIconButton*                       m_toolTo = nullptr;
    JPIconButton*                       m_captureCamera = nullptr;
    JPIconButton*                       m_captureTool = nullptr;
    JPIconButton*                       m_twoPoint = nullptr;
    JPIconButton*                       m_fiducialCheck = nullptr;
    std::unique_ptr<JMenu>              m_addMenu, m_menu;
    std::vector<std::unique_ptr<JMenu>> m_subMenus;
};

} // inline namespace jf
