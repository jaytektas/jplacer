// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPBoardPanel.h"

#include "JPUiParts.h"

#include "common/JPlacerLog.h"

#include <j/core/Log.h>

inline namespace jf {

JPBoardPanel::JPBoardPanel(JSceneGraph& graph) : JContainer(graph) {
    JPUiParts::asPanel(*this);

    auto top = JPUiParts::row(graph);
    JButton* import = top->add(JPUiParts::button(graph, "Import Pick-and-Place\xE2\x80\xA6"));
    import->onClicked.connect([this] { if (onImport) onImport(); });
    add(std::move(top));
    // A board's name can be long (a file's): a line of its own.
    m_summary = add(std::make_unique<JLabel>(graph, "No board"));
    m_summary->setWordWrap(true);

    auto side = JPUiParts::row(graph);
    side->add(std::make_unique<JLabel>(graph, "Side up"));
    m_side = side->add(std::make_unique<JPChoiceRow>(graph, std::vector<std::string>{ "Top", "Bottom" }, 0));
    m_side->onChosen.connect([this](int i) {
        if (!m_updating && onSide) onSide(i == 1);
    });
    add(std::move(side));

    // The starting point: put the camera on one fiducial, say which.
    auto start = JPUiParts::row(graph);
    m_fiducial = start->add(std::make_unique<JComboBox>(graph, std::vector<std::string>{}));
    m_fiducial->setHSizePolicy(JSizePolicyMode::Expanding, 1);
    JButton* here = start->add(JPUiParts::button(graph, "Camera Is on It"));
    here->onClicked.connect([this] {
        if (onCameraOnFiducial && !m_fiducial->currentText().empty()) onCameraOnFiducial(m_fiducial->currentText());
    });
    add(std::move(start));

    auto locate = JPUiParts::row(graph);
    JButton* find = locate->add(JPUiParts::button(graph, "Locate Board"));
    find->onClicked.connect([this] { if (onLocate) onLocate(); });
    m_place = locate->add(std::make_unique<JLabel>(graph, ""));
    m_place->setHSizePolicy(JSizePolicyMode::Expanding, 1);
    m_place->setWordWrap(true);
    add(std::move(locate));

    m_found = add(std::make_unique<JListView>(graph));
    m_found->setVSizePolicy(JSizePolicyMode::Expanding, 1);

    // A board is square: what its fiducials show of the machine's lean can correct it.
    auto squareRow = JPUiParts::row(graph);
    m_square = squareRow->add(JPUiParts::button(graph, "Square the Machine\xE2\x80\xA6"));
    m_square->onClicked.connect([this] { if (onSquare) onSquare(); });
    m_square->setEnabled(false);
    add(std::move(squareRow));

    // Any placement: choose it, the camera goes to it.
    auto goTo = JPUiParts::row(graph);
    goTo->add(std::make_unique<JLabel>(graph, "Placements: double-click one to look at it"));
    add(std::move(goTo));
    m_placements = add(std::make_unique<JListView>(graph));
    m_placements->setVSizePolicy(JSizePolicyMode::Expanding, 2);
    m_placements->onItemActivated.connect([this](int i) {
        if (onGoTo && i >= 0 && size_t(i) < m_designators.size()) onGoTo(m_designators[size_t(i)]);
    });
    m_buttons = { import, here, find };
}

void JPBoardPanel::showBoard(const std::string& summary, bool bottom, const std::vector<std::string>& fiducials,
                             const std::vector<std::string>& placements, const std::vector<std::string>& designators) {
    m_updating = true;
    m_summary->setText(summary.empty() ? "No board" : summary);
    m_side->choose(bottom ? 1 : 0);
    m_fiducial->setItems(fiducials);
    m_placements->setItems(placements);
    m_designators = designators;
    m_updating = false;
}

void JPBoardPanel::showPlace(const std::string& text) {
    m_place->setText(text);
}

void JPBoardPanel::showFound(const std::vector<std::string>& lines) {
    m_found->setItems(lines);
}

void JPBoardPanel::setBusy(bool busy) {
    for (JButton* b : m_buttons) b->setEnabled(!busy);
    m_square->setEnabled(!busy && m_canSquare);
    m_side->setChoicesEnabled(!busy);
}

void JPBoardPanel::setCanSquare(bool can) {
    m_canSquare = can;
    m_square->setEnabled(can);
}

} // inline namespace jf
