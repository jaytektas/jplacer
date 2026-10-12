// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPGroupFrame.h"
#include "JPIconButton.h"
#include "JPBoardTools.h"
#include "JPFieldsModel.h"
#include "JPLinkedColumnsModel.h"
#include "JPPackagesTableModel.h"
#include "JPPartsTableModel.h"
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

class JPSearchField;

// The Job tab's Placements, as OpenPnP's JobPlacementsPanel: the chosen
// board's (or panel's) placements on its side facing up, with Placed and
// Status; a board's own (its ID, part, side, where it lies, type, comments:
// every use of it shares them) changed here, its use's (Enabled, Placed,
// Error Handling) for this use alone; the board's tools (JPBoardTools:
// Import Placements, Board's Parts, View Board, Revision); New and Remove
// Placement(s) (a board's), Move Camera To (and To Next) Placement Location, Move Tool To
// Placement Location, Capture Camera and Tool Placement Location, Edit
// Placement Feeder, a search box, and the right-click Set Type, Set Side,
// Set Placed, Set Enabled, Set Error Handling. Space turns the chosen
// placement on or off. Its Columns… offer each placement's part's columns
// (the Parts tab's, its fields with them) and its part's package's (the
// Packages tab's), headed Part ▸ and Package ▸: edited here, the library's
// part or package is changed, for every placement of it.
class JPJobPlacementsPanel : public JPGroupFrame {
public:
    enum class Tool { Camera, Nozzle };

    JPJobPlacementsPanel(JSceneGraph& graph, JPConfiguration& config, std::function<JPJob*()> job);

    // The job or a board changed.
    std::function<void()> onChanged;
    // A library part or package changed (in a Part ▸ or Package ▸ column).
    std::function<void()> onLibraryChanged;
    std::function<void(JMenu*, float x, float y)> openMenu;
    // Where a tool is now (machine millimetres), and taking it to a place at safe Z.
    std::function<std::optional<JPLocation>(Tool)> toolLocation;
    std::function<void(Tool, const JPLocation&)> moveTool;
    // The chosen placement's part's package footprint, drawn over the cameras (null: none chosen), as
    // OpenPnP's linked tables choose its package and so its reticle; turned as the selected tool is (a
    // camera moved to the placement, to its rotation). Told when the placement chosen changes, not when
    // the tab is left: it stays until something else is.
    std::function<void(const JPFootprint* footprint)> showFootprint;
    // The placements placed, of all and of the board shown (the status line).
    std::function<void(int placed, int total, int boardPlaced, int boardTotal)> onCompletion;

    // The board or panel of the job shown, or none.
    void setLocation(JPPlacementsHolderLocation* location);
    JPPlacementsHolderLocation* location() const { return m_location; }
    void refresh();
    // A placement chosen (and shown), by id.
    void select(const std::string& placementId);
    // The placement of an id chosen, the search kept (one not shown: none chosen).
    void selectPlacement(const std::string& placementId);
    // The one placement chosen, or none (nothing or several), as the table's choice changes.
    std::function<void(const JPPlacement*)> onPlacementChosen;
    // Edit Placement Feeder: the Feeders tab showing the part's feeder.
    std::function<void(const std::string& partId)> onEditFeeder;
    // The placed counts given again (OpenPnP's updateActivePlacements).
    void updateActivePlacements();
    JPPlacementsTableModel& model() { return m_model; }
    // The tools of the board shown (none: a panel's, or nothing chosen).
    JPBoardTools& boardTools() { return *m_boardTools; }

    // The placements chosen, in the order shown.
    std::vector<JPPlacement*> selections() const;

private:
    void updateActions();
    void buildMenu();
    void newPlacement();
    void removePlacements();
    // The chosen placement, or the row after it or before it as the table shows them (chosen first): the tool
    // taken there.
    enum class Step { Chosen, Next, Previous };
    void moveTo(Tool tool, Step step);
    void capture(Tool tool);
    void changed();

    JPConfiguration&                    m_config;
    std::function<JPJob*()>             m_job;
    JPPlacementsTableModel              m_model;
    // The Part ▸ and Package ▸ columns: the Parts and Packages tabs' tables, their fields with them.
    JPPartsTableModel                   m_parts;
    JPFieldsModel                       m_partFields;
    JPPackagesTableModel                m_packages;
    JPFieldsModel                       m_packageFields;
    JPLinkedColumnsModel                m_linked;
    // The library's row of a placement row's part, and of its package (-1: none).
    int partRowOf(int row) const;
    int packageRowOf(int row) const;
    // A library part or package changed here: said, with how many placements here are of it.
    void libraryEdited(const std::string& what, const std::string& name, int placementsHere);
    JPPlacementsHolderLocation*         m_location = nullptr;
    std::string                         m_footprintShown;   // what showFootprint was last told of ("": none)
    JPTable*                            m_table = nullptr;
    JPSearchField*                          m_search = nullptr;
    JPIconButton*                       m_new = nullptr;
    JPBoardTools* m_boardTools = nullptr;
    JPIconButton*                       m_remove = nullptr;
    JPIconButton*                       m_cameraTo = nullptr;
    JPIconButton*                       m_cameraNext = nullptr;
    JPIconButton*                       m_cameraPrevious = nullptr;
    JPIconButton*                       m_toolTo = nullptr;
    JPIconButton*                       m_captureCamera = nullptr;
    JPIconButton*                       m_captureTool = nullptr;
    JPIconButton*                       m_editFeeder = nullptr;
    JPIconButton*                       m_turn = nullptr;
    JPIconButton*                       m_verify = nullptr;
    // The placement's footprint: its part's package's (OpenPnP's reticle is the package's).
    const JPFootprint* footprintOf(const JPPlacement& p) const;
    // showFootprint told of the chosen placement's when it is not what it was last told.
    void showChosenFootprint();
    // The chosen placement turned a quarter (its correction, kept apart from the CAD's rotation).
    void turnChosen();
    // The chosen placement marked verified by the operator; the camera to the next not verified.
    void verifyChosen();
    std::unique_ptr<JMenu>              m_menu;
    std::vector<std::unique_ptr<JMenu>> m_subMenus;
    JMenuItem*                          m_setType = nullptr;
    JMenuItem*                          m_setSide = nullptr;
};

} // inline namespace jf
