// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPIconButton.h"

#include "model/JPBoard.h"
#include "model/JPBoardImporter.h"
#include "model/JPConfiguration.h"
#include "model/JPJob.h"

#include <j/core/JComboBox.h>
#include <j/core/JContainer.h>
#include <j/core/MenuSystem.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

inline namespace jf {

// The tools of the board a job's Placements show (JPJobPlacementsPanel's toolbar): Import Placements (CPL and
// BOM…, OpenPnP's importers: merged into the board, or replacing its placements, or made its next revision),
// Board's Parts, View Board (the board viewer), and the Revision shown. What it changes is the board itself,
// every use of it.
class JPBoardTools : public JContainer {
public:
    JPBoardTools(JSceneGraph& graph, JPConfiguration& config, std::function<const JPJob*()> job);
    // The board changed (an import taken, a revision shown), or parts made by an import.
    std::function<void()> onChanged;
    // Opens a menu at window coordinates.
    std::function<void(JMenu*, float x, float y)> openMenu;
    // View Board: the viewer of the board.
    std::function<void()> onViewBoard;
    // Opens an importer's dialog; `imported` has what it read.
    std::function<void(const JPBoardImporter&, std::function<void(JPBoard&)> imported)> openImporter;
    // Opens a board's parts (JPlacerBoardPartsDialog): Board's Parts, and after an import that left parts to choose.
    std::function<void(JPBoard&)> openBoardParts;
    // Opens the CPL and BOM import (JPlacerCplBomImportDialog); `imported` has the board it made.
    std::function<void(std::function<void(JPBoard&)> imported)> openCplBom;
    // An import taken (its parts and packages the library's now): `imported` as it was read.
    std::function<void(const JPBoard& imported)> onImportTaken;
    // Opens a new revision's upgrade (JPlacerBoardUpgradeDialog) of the board from `files`; `made` has the new
    // revision and the label of the one shown now (for a board that kept none).
    std::function<void(JPBoard& board, std::shared_ptr<JPBoard> files,
                       std::function<void(JPBoardRevision, std::string)> made)> openUpgrade;
    // Asks a question with buttons of its own; the index chosen, -1 closed.
    std::function<void(const std::string& title, const std::string& question, std::vector<std::string> options,
                       int cancelIndex, std::function<void(int)> chosen)> askChoice;
    // The board the tools are of (a definition), or none.
    void setBoard(JPBoard* board);
    JPBoard* board() const { return m_board; }
    // The board changed elsewhere: its revisions shown again.
    void refresh();
    // Imports into the board (Import Placements, File > Import Placements): an error when there is none.
    // From the CAD files themselves: a placement file and its BOM (and other tables).
    void importCplBom();
    void importBoard(const JPBoardImporter& importer);
    const std::vector<std::unique_ptr<JPBoardImporter>>& importers() const { return m_importers; }
private:
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
    void changed();
    JPConfiguration&                              m_config;
    std::function<const JPJob*()>                 m_job;
    JPBoard*                                      m_board = nullptr;
    std::vector<std::unique_ptr<JPBoardImporter>> m_importers;
    JPIconButton*                                 m_import = nullptr;
    JPIconButton*                                 m_view = nullptr;
    JPIconButton*                                 m_parts = nullptr;
    JComboBox*                                    m_revision = nullptr;
    bool                                          m_fillingRevisions = false;
    bool                                          m_partsAfterMerge = false;   // an import left parts to choose
    std::unique_ptr<JMenu>                        m_importMenu;
};
} // inline namespace jf
