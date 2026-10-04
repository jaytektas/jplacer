// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPGroupFrame.h"
#include "JPIconButton.h"
#include "JPPlacementsTableModel.h"
#include "JPTable.h"

#include "model/JPConfiguration.h"
#include "model/JPJob.h"

#include <j/core/JLineEdit.h>
#include <j/core/MenuSystem.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

inline namespace jf {

// The Job tab's Placements, as OpenPnP's JobPlacementsPanel: the chosen
// board's (or panel's) placements on its side facing up, with Placed and
// Status; New and Remove Placement(s) (a board used once, straight in the
// job), Move Camera To (and To Next) Placement Location, Move Tool To
// Placement Location, Capture Camera and Tool Placement Location, Edit
// Placement Feeder, a search box, and the right-click Set Type, Set Side,
// Set Placed, Set Enabled, Set Error Handling. Space turns the chosen
// placement on or off.
class JPJobPlacementsPanel : public JPGroupFrame {
public:
    enum class Tool { Camera, Nozzle };

    JPJobPlacementsPanel(JSceneGraph& graph, JPConfiguration& config, std::function<JPJob*()> job);

    // The job or a board changed.
    std::function<void()> onChanged;
    std::function<void(JMenu*, float x, float y)> openMenu;
    // Where a tool is now (machine millimetres), and taking it to a place at safe Z.
    std::function<std::optional<JPLocation>(Tool)> toolLocation;
    std::function<void(Tool, const JPLocation&)> moveTool;
    // The placements placed, of all and of the board shown (the status line).
    std::function<void(int placed, int total, int boardPlaced, int boardTotal)> onCompletion;

    // The board or panel of the job shown, or none.
    void setLocation(JPPlacementsHolderLocation* location);
    JPPlacementsHolderLocation* location() const { return m_location; }
    void refresh();
    // A placement chosen (and shown), by id.
    void select(const std::string& placementId);
    // Edit Placement Feeder: the Feeders tab showing the part's feeder.
    std::function<void(const std::string& partId)> onEditFeeder;
    // The placed counts given again (OpenPnP's updateActivePlacements).
    void updateActivePlacements();
    JPPlacementsTableModel& model() { return m_model; }

private:
    std::vector<JPPlacement*> selections() const;
    void updateActions();
    void buildMenu();
    void newPlacement();
    void removePlacements();
    void moveTo(Tool tool, bool next);
    void capture(Tool tool);
    void changed();

    JPConfiguration&                    m_config;
    std::function<JPJob*()>             m_job;
    JPPlacementsTableModel              m_model;
    JPPlacementsHolderLocation*         m_location = nullptr;
    bool                                m_topLevel = false, m_singleInstance = false;
    JPTable*                            m_table = nullptr;
    JLineEdit*                          m_search = nullptr;
    JPIconButton*                       m_new = nullptr;
    JPIconButton*                       m_remove = nullptr;
    JPIconButton*                       m_cameraTo = nullptr;
    JPIconButton*                       m_cameraNext = nullptr;
    JPIconButton*                       m_toolTo = nullptr;
    JPIconButton*                       m_captureCamera = nullptr;
    JPIconButton*                       m_captureTool = nullptr;
    JPIconButton*                       m_editFeeder = nullptr;
    std::unique_ptr<JMenu>              m_menu;
    std::vector<std::unique_ptr<JMenu>> m_subMenus;
    JMenuItem*                          m_setType = nullptr;
    JMenuItem*                          m_setSide = nullptr;
};

} // inline namespace jf
