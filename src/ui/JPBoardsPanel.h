// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPBoardPlacementsPanel.h"
#include "JPPlacementsHoldersGroup.h"

#include "model/JPConfiguration.h"
#include "model/JPJob.h"

#include <j/core/JContainer.h>
#include <j/core/MenuSystem.h>
#include <j/core/Splitter.h>

#include <functional>
#include <memory>

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
    // Asks whether to save a changed board before it is taken away.
    std::function<void(JPPlacementsHolder&, std::function<void()> then)> confirmSave;
    // The board whose placements are shown changed (the viewer follows it).
    std::function<void(JPBoard*)> onBoardShown;

    JPBoardPlacementsPanel& placements() { return *m_placements; }
    // The boards changed elsewhere: shown again, the selection kept.
    void refresh();
    void selectBoard(const JPBoard* board) { m_boards->select(board); }
    double split() const;

private:
    std::unique_ptr<JContainer> m_boardsPane, m_placementsPane;
    JPPlacementsHoldersGroup*   m_boards = nullptr;
    JPBoardPlacementsPanel*     m_placements = nullptr;
    JSplitter*                  m_split = nullptr;
};

} // inline namespace jf
