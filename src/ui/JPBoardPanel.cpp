// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPBoardPanel.h"

#include "JPUiParts.h"

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

    // How the references are captured: found by the camera, or recorded by hand.
    auto capture = JPUiParts::row(graph);
    capture->add(std::make_unique<JLabel>(graph, "References"));
    m_capture = capture->add(std::make_unique<JPChoiceRow>(graph, std::vector<std::string>{ "Found by Camera", "Recorded by Hand" }, 0));
    m_capture->onChosen.connect([this](int i) {
        if (!m_updating && onCapture) onCapture(i == 1);
    });
    add(std::move(capture));
    auto newBoard = JPUiParts::row(graph);
    m_newBoardLabel = newBoard->add(std::make_unique<JLabel>(graph, "A new board"));
    m_newBoard = newBoard->add(std::make_unique<JPChoiceRow>(graph, std::vector<std::string>{ "Record Again", "Reuse Them" }, 0));
    m_newBoard->onChosen.connect([this](int i) {
        if (!m_updating && onNewBoard) onNewBoard(i == 1);
    });
    JButton* onBed = newBoard->add(JPUiParts::button(graph, "New Board on the Bed"));
    onBed->onClicked.connect([this] { if (onNewBoardOnBed) onNewBoardOnBed(); });
    add(std::move(newBoard));

    // The references, each with what was last captured of it.
    add(std::make_unique<JLabel>(graph, "References on this side: choose one; double-click to look at it"));
    m_references = add(std::make_unique<JListView>(graph));
    m_references->setVSizePolicy(JSizePolicyMode::Expanding, 1);
    m_references->onItemActivated.connect([this](int i) {
        if (onGoTo && i >= 0 && size_t(i) < m_referenceKeys.size()) onGoTo(m_referenceKeys[size_t(i)]);
    });
    auto refButtons = JPUiParts::row(graph);
    auto withChosen = [this](std::function<void(const std::string&)>& f) {
        return [this, &f] {
            const std::string d = chosenReference();
            if (f && !d.empty()) f(d);
        };
    };
    JButton* goTo = refButtons->add(JPUiParts::button(graph, "Go To"));
    goTo->onClicked.connect(withChosen(onGoTo));
    JButton* record = refButtons->add(JPUiParts::button(graph, "Record"));
    record->onClicked.connect(withChosen(onRecord));
    JButton* here = refButtons->add(JPUiParts::button(graph, "Camera Is on It"));
    here->onClicked.connect(withChosen(onCameraOnReference));
    add(std::move(refButtons));

    auto locate = JPUiParts::row(graph);
    JButton* find = locate->add(JPUiParts::button(graph, "Locate Board"));
    find->onClicked.connect([this] { if (onLocate) onLocate(); });
    m_place = locate->add(std::make_unique<JLabel>(graph, ""));
    m_place->setHSizePolicy(JSizePolicyMode::Expanding, 1);
    m_place->setWordWrap(true);
    add(std::move(locate));

    // A board is square: what its references show of the machine's lean can correct it.
    auto squareRow = JPUiParts::row(graph);
    m_square = squareRow->add(JPUiParts::button(graph, "Square the Machine\xE2\x80\xA6"));
    m_square->onClicked.connect([this] { if (onSquare) onSquare(); });
    m_square->setEnabled(false);
    m_lean = squareRow->add(std::make_unique<JLabel>(graph, ""));
    m_lean->setHSizePolicy(JSizePolicyMode::Expanding, 1);
    m_lean->setWordWrap(true);
    add(std::move(squareRow));

    // Any placement: choose it, the camera goes to it.
    auto goToAny = JPUiParts::row(graph);
    goToAny->add(std::make_unique<JLabel>(graph, "Placements: double-click one to look at it"));
    add(std::move(goToAny));
    m_placements = add(std::make_unique<JListView>(graph));
    m_placements->setVSizePolicy(JSizePolicyMode::Expanding, 2);
    m_placements->onItemActivated.connect([this](int i) {
        if (onGoTo && i >= 0 && size_t(i) < m_designators.size()) onGoTo(m_designators[size_t(i)]);
    });
    m_buttons = { import, goTo, record, here, find, onBed };
}

void JPBoardPanel::showBoard(const std::string& summary, bool bottom, const std::vector<std::string>& references,
                             const std::vector<std::string>& referenceKeys, const std::vector<std::string>& placements,
                             const std::vector<std::string>& designators) {
    m_updating = true;
    const std::string chosen = chosenReference();
    m_summary->setText(summary.empty() ? "No board" : summary);
    m_side->choose(bottom ? 1 : 0);
    m_references->setItems(references);
    m_referenceKeys = referenceKeys;
    for (size_t i = 0; i < referenceKeys.size(); ++i)
        if (referenceKeys[i] == chosen) m_references->setSelectedIndex(int(i));
    m_placements->setItems(placements);
    m_designators = designators;
    m_updating = false;
}

void JPBoardPanel::showCapture(bool byHand, bool reuse) {
    m_updating = true;
    m_capture->choose(byHand ? 1 : 0);
    m_newBoard->choose(reuse ? 1 : 0);
    m_newBoard->setChoicesEnabled(byHand);
    m_newBoardLabel->setEnabled(byHand);
    m_updating = false;
}

std::string JPBoardPanel::chosenReference() const {
    const int i = m_references->selectedIndex();
    return i >= 0 && size_t(i) < m_referenceKeys.size() ? m_referenceKeys[size_t(i)] : std::string();
}

void JPBoardPanel::chooseReference(const std::string& designator) {
    for (size_t i = 0; i < m_referenceKeys.size(); ++i)
        if (m_referenceKeys[i] == designator) m_references->setSelectedIndex(int(i));
}

void JPBoardPanel::showPlace(const std::string& text) {
    m_place->setText(text);
}

void JPBoardPanel::setBusy(bool busy) {
    for (JButton* b : m_buttons) b->setEnabled(!busy);
    m_square->setEnabled(!busy && m_canSquare);
    m_side->setChoicesEnabled(!busy);
    m_capture->setChoicesEnabled(!busy);
}

void JPBoardPanel::showLean(const std::string& text) {
    m_lean->setText(text);
    m_canSquare = !text.empty();
    m_square->setEnabled(m_canSquare);
}

} // inline namespace jf
