// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPBoardPlacementsPanel.h"
#include "JPIconButton.h"
#include "JPPlacementsHolderTableModel.h"
#include "JPTable.h"

#include "model/JPConfiguration.h"
#include "model/JPJob.h"

#include <j/core/JContainer.h>
#include <j/core/MenuSystem.h>
#include <j/core/Splitter.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

inline namespace jf {

// The Boards tab, as OpenPnP's BoardsPanel: the Boards group (Add Board
// (Create New Board…, Existing Board), Remove Board, Copy Board…, Clean Up;
// the boards known, by name and size) over the chosen board's Placements
// (JPBoardPlacementsPanel), a divider between to drag.
class JPBoardsPanel : public JContainer {
public:
    // `split`: the boards' share of the height, as last left.
    JPBoardsPanel(JSceneGraph& graph, JPConfiguration& config, std::function<const JPJob*()> job, double split);

    // A board added, changed or taken away (to be saved, other views told).
    std::function<void()> onChanged;
    std::function<void(JMenu*, float x, float y)> openMenu;
    // Asks whether to save a changed board before it is taken away: Yes,
    // No or Cancel (OpenPnP's confirmSaveOfModified); `then` runs after.
    std::function<void(JPPlacementsHolder&, std::function<void()> then)> confirmSave;

    JPBoardPlacementsPanel& placements() { return *m_placements; }
    // The boards changed elsewhere: shown again, the selection kept.
    void refresh();
    void selectBoard(const JPBoard* board);
    JPBoard* selection() const;
    double split() const;

private:
    std::vector<JPBoard*> selections() const;
    void selectionChanged();
    void showAddMenu();
    void addBoard(const std::string& path, const char* errorTitle);
    void removeBoards(std::vector<JPBoard*> boards, bool reportInUse);
    void copyBoard();
    void changed();

    JPConfiguration&                        m_config;
    std::function<const JPJob*()>           m_job;
    JPPlacementsHolderTableModel            m_model;
    JPTable*                                m_table = nullptr;
    JSplitter*                              m_split = nullptr;
    std::unique_ptr<JContainer>             m_boardsPane, m_placementsPane;
    JPBoardPlacementsPanel*                 m_placements = nullptr;
    JPIconButton*                           m_add = nullptr;
    JPIconButton*                           m_remove = nullptr;
    JPIconButton*                           m_copy = nullptr;
    std::unique_ptr<JMenu>                  m_addMenu;
};

} // inline namespace jf
