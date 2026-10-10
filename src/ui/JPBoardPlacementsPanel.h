// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPIconButton.h"
#include "JPPlacementsTableModel.h"
#include "JPTable.h"

#include "model/JPBoardImporter.h"
#include "model/JPConfiguration.h"
#include "model/JPJob.h"

#include <j/core/JComboBox.h>
#include <j/core/JContainer.h>
#include <j/core/JLineEdit.h>
#include <j/core/MenuSystem.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

inline namespace jf {

// The Boards tab's Placements group, as OpenPnP's BoardPlacementsPanel: a
// toolbar (New Placement, Remove Placement(s), Import Placements, View
// Board: the board viewer), a search box, and the chosen board's placements (all but Placed
// and Status), with OpenPnP's right-click menu (Set Type, Set Side, Set
// Enabled, Set Error Handling) and Space turning the chosen placement on or
// off.
class JPBoardPlacementsPanel : public JContainer {
public:
    JPBoardPlacementsPanel(JSceneGraph& graph, JPConfiguration& config, std::function<const JPJob*()> job);

    // A placement added, changed or taken away (or parts made by an import).
    std::function<void()> onChanged;
    // Opens a menu at window coordinates.
    std::function<void(JMenu*, float x, float y)> openMenu;
    // View Board: the viewer of the board shown.
    std::function<void()> onViewBoard;
    // Opens an importer's dialog; `imported` has what it read.
    std::function<void(const JPBoardImporter&, std::function<void(JPBoard&)> imported)> openImporter;
    // Opens a board's parts (JPlacerBoardPartsDialog): Board's Parts, and after an import that left parts to choose.
    std::function<void(JPBoard&)> openBoardParts;
    // Opens the CPL and BOM import (JPlacerCplBomImportDialog); `imported` has the board it made.
    std::function<void(std::function<void(JPBoard&)> imported)> openCplBom;
    // Opens a new revision's upgrade (JPlacerBoardUpgradeDialog) of the board from `files`; `made` has the new
    // revision and the label of the one shown now (for a board that kept none).
    std::function<void(JPBoard& board, std::shared_ptr<JPBoard> files,
                       std::function<void(JPBoardRevision, std::string)> made)> openUpgrade;
    // Asks a question with buttons of its own; the index chosen, -1 closed.
    std::function<void(const std::string& title, const std::string& question, std::vector<std::string> options,
                       int cancelIndex, std::function<void(int)> chosen)> askChoice;

    // Its placements' table model (the tabs give it the part picker).
    JPPlacementsTableModel& model() { return m_model; }
    // The board whose placements are shown (a definition), or none.
    void setBoard(JPBoard* board);
    JPBoard* board() const { return m_board; }
    // The placements changed elsewhere: shown again.
    void refresh();
    // The placement of an id chosen; none chosen for an empty id or one not shown.
    void selectPlacement(const std::string& id);
    // The one placement chosen (none: nothing or several chosen), as the table's choice changes.
    std::function<void(const JPPlacement*)> onPlacementChosen;
    // Imports into the board shown with `importer` (Import Placements, File
    // > Import Board): an error when no board is chosen.
    // From the CAD files themselves: a placement file and its BOM (and other tables), into the board shown.
    void importCplBom();
    void importBoard(const JPBoardImporter& importer);
    const std::vector<std::unique_ptr<JPBoardImporter>>& importers() const { return m_importers; }

private:
    std::vector<JPPlacement*> selections() const;
    void updateActions();
    void newPlacement();
    void removePlacements();
    // An import's board into `board` (still the one shown): merged, or after asking, replacing what it has.
    void take(JPBoard* board, JPBoard& imported);
    void merge(JPBoard& imported);
    // An import's board as the board's new revision, after the upgrade's summary.
    void upgrade(JPBoard* board, std::shared_ptr<JPBoard> files);
    // An import taken: the library parts and packages it made (Create Missing Parts) put in the library.
    void takeMade(const JPBoard& imported);
    // The revision chooser: the board's revisions, the one shown chosen.
    void fillRevisions();
    void switchRevision(int index);
    void showImportMenu();
    void buildContextMenu();
    void changed();

    JPConfiguration&                              m_config;
    std::function<const JPJob*()>                 m_job;
    JPPlacementsTableModel                        m_model;
    JPBoard*                                      m_board = nullptr;
    std::vector<std::unique_ptr<JPBoardImporter>> m_importers;
    JPTable*                                      m_table = nullptr;
    JLineEdit*                                    m_search = nullptr;
    JPIconButton*                                 m_new = nullptr;
    JPIconButton*                                 m_remove = nullptr;
    JPIconButton*                                 m_import = nullptr;
    JPIconButton*                                 m_view = nullptr;
    JPIconButton*                                 m_parts = nullptr;
    JComboBox*                                    m_revision = nullptr;
    bool                                          m_fillingRevisions = false;
    bool                                          m_partsAfterMerge = false;   // an import left parts to choose
    std::unique_ptr<JMenu>                        m_importMenu;
    std::unique_ptr<JMenu>                        m_contextMenu;
    std::vector<std::unique_ptr<JMenu>>           m_subMenus;
};

} // inline namespace jf
