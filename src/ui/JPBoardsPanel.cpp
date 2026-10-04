// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPBoardsPanel.h"

#include "JPGroupFrame.h"

inline namespace jf {

JPBoardsPanel::JPBoardsPanel(JSceneGraph& graph, JPConfiguration& config, std::function<const JPJob*()> job,
                             double split)
    : JContainer(graph, 0.f, 0.f) {
    setDirection(JFlexDirection::Column)->setAlignItems(JAlignItems::Stretch);

    m_boardsPane = std::make_unique<JContainer>(graph, 0.f, 0.f);
    m_boardsPane->setDirection(JFlexDirection::Column)->setAlignItems(JAlignItems::Stretch);
    m_boards = m_boardsPane->add(std::make_unique<JPPlacementsHoldersGroup>(graph, config, JPPlacementsHolder::Kind::Board, job));
    m_boards->openMenu = [this](JMenu* m, float x, float y) { if (openMenu) openMenu(m, x, y); };
    m_boards->confirmSave = [this](JPPlacementsHolder& h, std::function<void()> then) {
        if (confirmSave) confirmSave(h, std::move(then));
        else then();
    };
    m_boards->onChanged = [this] {
        m_placements->refresh();
        if (onChanged) onChanged();
    };
    m_boards->onShown = [this](JPPlacementsHolder* h) {
        auto* b = static_cast<JPBoard*>(h);
        m_placements->setBoard(b);
        if (onBoardShown) onBoardShown(b);
    };

    // Placements: the chosen board's.
    m_placementsPane = std::make_unique<JContainer>(graph, 0.f, 0.f);
    m_placementsPane->setDirection(JFlexDirection::Column)->setAlignItems(JAlignItems::Stretch);
    auto placements = std::make_unique<JPGroupFrame>(graph, "Placements");
    placements->setAlignItems(JAlignItems::Stretch);
    placements->setVSizePolicy(JSizePolicyMode::Expanding, 1);
    m_placements = placements->add(std::make_unique<JPBoardPlacementsPanel>(graph, config, job));
    m_placements->setVSizePolicy(JSizePolicyMode::Expanding, 1);
    m_placements->openMenu = [this](JMenu* m, float x, float y) { if (openMenu) openMenu(m, x, y); };
    m_placements->onChanged = [this] {
        m_boards->refresh();
        if (onChanged) onChanged();
    };
    m_placementsPane->add(std::move(placements));

    m_split = add(std::make_unique<JSplitter>(graph, JSplitter::JOrientation::Vertical, 0.f, 0.f));
    m_split->setHostsPanes(true);
    m_split->setVSizePolicy(JSizePolicyMode::Expanding, 1);
    m_split->addPane(m_boardsPane.get(), float(split));
    m_split->addPane(m_placementsPane.get(), float(1 - split));
}

double JPBoardsPanel::split() const {
    const std::vector<float> f = m_split->fractions();
    return f.empty() ? 0.5 : f.front();
}

void JPBoardsPanel::refresh() {
    m_boards->refresh();
    m_placements->refresh();
}

} // inline namespace jf
