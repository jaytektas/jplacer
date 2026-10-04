// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPIconButton.h"
#include "JPPlacementsTableModel.h"
#include "JPTable.h"

#include "model/JPBoardImporter.h"
#include "model/JPConfiguration.h"
#include "model/JPJob.h"

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
    // Asks a question with buttons of its own; the index chosen, -1 closed.
    std::function<void(const std::string& title, const std::string& question, std::vector<std::string> options,
                       int cancelIndex, std::function<void(int)> chosen)> askChoice;

    // The board whose placements are shown (a definition), or none.
    void setBoard(JPBoard* board);
    JPBoard* board() const { return m_board; }
    // The placements changed elsewhere: shown again.
    void refresh();
    // Imports into the board shown with `importer` (Import Placements, File
    // > Import Board): an error when no board is chosen.
    void importBoard(const JPBoardImporter& importer);
    const std::vector<std::unique_ptr<JPBoardImporter>>& importers() const { return m_importers; }

private:
    std::vector<JPPlacement*> selections() const;
    void updateActions();
    void newPlacement();
    void removePlacements();
    void merge(JPBoard& imported);
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
    std::unique_ptr<JMenu>                        m_importMenu;
    std::unique_ptr<JMenu>                        m_contextMenu;
    std::vector<std::unique_ptr<JMenu>>           m_subMenus;
};

} // inline namespace jf
