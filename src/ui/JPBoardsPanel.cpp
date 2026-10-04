// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPBoardsPanel.h"

#include "JPGroupFrame.h"
#include "JPUiParts.h"

#include <j/core/Dialog.h>
#include <j/core/JSeparator.h>

#include <algorithm>
#include <cctype>
#include <filesystem>

inline namespace jf {

namespace {

constexpr const char* kBoardSuffix = ".board.xml";

std::unique_ptr<JSeparator> toolSeparator(JSceneGraph& graph) {
    return std::make_unique<JSeparator>(graph, JSeparator::JOrientation::Vertical, JPIconButton::size());
}

// A file name chosen to save as, given ".board.xml" when it lacks it.
std::string withBoardSuffix(std::string path) {
    std::string lower = path;
    for (char& c : lower) c = char(std::tolower(uint8_t(c)));
    const std::string s = kBoardSuffix;
    if (lower.size() < s.size() || lower.compare(lower.size() - s.size(), s.size(), s) != 0) path += s;
    return path;
}

} // namespace

JPBoardsPanel::JPBoardsPanel(JSceneGraph& graph, JPConfiguration& config, std::function<const JPJob*()> job,
                             double split)
    : JContainer(graph, 0.f, 0.f), m_config(config), m_job(job), m_model(config, JPPlacementsHolder::Kind::Board, job) {
    setDirection(JFlexDirection::Column)->setAlignItems(JAlignItems::Stretch);
    m_model.onChanged = [this] { changed(); };

    // Boards: the toolbar over the table.
    m_boardsPane = std::make_unique<JContainer>(graph, 0.f, 0.f);
    m_boardsPane->setDirection(JFlexDirection::Column)->setAlignItems(JAlignItems::Stretch);
    auto boards = std::make_unique<JPGroupFrame>(graph, "Boards");
    boards->setAlignItems(JAlignItems::Stretch);
    boards->setVSizePolicy(JSizePolicyMode::Expanding, 1);
    auto bar = JPUiParts::row(graph);
    auto tool = [&](const char* name, const char* icon, const char* tip) {
        return bar->add(std::make_unique<JPIconButton>(graph, name, icon, tip));
    };
    m_add = tool("Add Board...", "general-add", "Add a new or existing board");
    m_add->setLeads(JPIconButton::Leads::Menu);
    m_add->onClicked.connect([this] { showAddMenu(); });
    m_remove = tool("Remove Board", "general-remove", "Remove the selected board(s)");
    m_remove->onClicked.connect([this] { removeBoards(selections(), true); });
    m_copy = tool("Copy Board...", "copy", "Copy the selected board");
    m_copy->setLeads(JPIconButton::Leads::Elsewhere);
    m_copy->onClicked.connect([this] { copyBoard(); });
    bar->add(toolSeparator(graph));
    tool("Clean Up", "clean-sweep",
         "Remove any boards that are not in use by the currently loaded job or by any currently loaded panel. Does "
         "not delete them from the file system.")
        ->onClicked.connect([this] {
            std::vector<JPBoard*> unused;
            for (const auto& b : m_config.boards())
                if (!m_config.isInUse(*b, m_job())) unused.push_back(b.get());
            removeBoards(unused, false);
        });
    boards->add(std::move(bar));
    m_table = boards->add(std::make_unique<JPTable>(graph));
    m_table->setModel(&m_model);
    m_table->openMenu = [this](JMenu* m, float x, float y) { if (openMenu) openMenu(m, x, y); };
    m_table->onSelectionChanged.connect([this] { selectionChanged(); });
    m_table->onEditRefused = [](const std::string&) {};
    m_boardsPane->add(std::move(boards));

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
        m_table->refresh();
        changed();
    };
    m_placementsPane->add(std::move(placements));

    m_split = add(std::make_unique<JSplitter>(graph, JSplitter::JOrientation::Vertical, 0.f, 0.f));
    m_split->setHostsPanes(true);
    m_split->setVSizePolicy(JSizePolicyMode::Expanding, 1);
    m_split->addPane(m_boardsPane.get(), float(split));
    m_split->addPane(m_placementsPane.get(), float(1 - split));
    selectionChanged();
}

double JPBoardsPanel::split() const {
    const std::vector<float> f = m_split->fractions();
    return f.empty() ? 0.5 : f.front();
}

void JPBoardsPanel::refresh() {
    m_table->refresh();
    selectionChanged();
    m_placements->refresh();
}

std::vector<JPBoard*> JPBoardsPanel::selections() const {
    std::vector<JPBoard*> out;
    for (const int r : m_table->selectedRows())
        if (auto* b = static_cast<JPBoard*>(m_model.holder(r))) out.push_back(b);
    return out;
}

JPBoard* JPBoardsPanel::selection() const {
    const auto s = selections();
    return s.empty() ? nullptr : s.front();
}

void JPBoardsPanel::selectBoard(const JPBoard* board) {
    if (!board) {
        m_table->clearSelection();
        return;
    }
    if (const int r = m_model.rowOf(board); r >= 0) m_table->selectRow(r);
}

void JPBoardsPanel::selectionChanged() {
    const auto s = selections();
    // As OpenPnP's action groups: Remove for one or more, Copy for one.
    m_remove->setEnabled(!s.empty());
    m_copy->setEnabled(s.size() == 1);
    JPBoard* shown = s.size() == 1 ? s.front() : nullptr;
    if (m_placements->board() != shown) m_placements->setBoard(shown);
}

void JPBoardsPanel::changed() {
    if (onChanged) onChanged();
}

void JPBoardsPanel::showAddMenu() {
    if (!openMenu) return;
    m_addMenu = std::make_unique<JMenu>("Add Board");
    m_addMenu->add(m_graph, "Create New Board...")->onTriggered.connect([this] {
        JDialog::saveFile("Save New Board As...", { "xml" }, [this](std::string path) {
            addBoard(withBoardSuffix(path), "Unable to create new board");
        });
    });
    m_addMenu->add(m_graph, "Existing Board")->onTriggered.connect([this] {
        JDialog::openFile("Existing Board", { "xml" }, [this](std::string path) { addBoard(path, "Board load failed"); });
    });
    const JRect b = m_graph.getLayoutConst(m_add->getNodeId()).boundingBox;
    openMenu(m_addMenu.get(), b.x + b.width, b.y + b.height);
}

void JPBoardsPanel::addBoard(const std::string& path, const char* errorTitle) {
    std::string error;
    const auto board = m_config.board(path, error);
    if (!board) {
        JDialog::message(errorTitle, error);
        return;
    }
    m_table->refresh();
    selectBoard(board.get());
    changed();
}

void JPBoardsPanel::removeBoards(std::vector<JPBoard*> boards, bool reportInUse) {
    if (boards.empty()) {
        m_table->refresh();
        selectionChanged();
        changed();
        return;
    }
    JPBoard* board = boards.front();
    boards.erase(boards.begin());
    auto next = [this, boards, reportInUse] { removeBoards(boards, reportInUse); };
    if (m_config.isInUse(*board, m_job())) {
        if (reportInUse) {
            JDialog::message("Error Removing Board",
                             "Could not remove " + board->name.value_or("") +
                                 " because it is either being used by the current job or by a panel that is loaded "
                                 "in the current configuration.",
                             next);
            return;
        }
        next();
        return;
    }
    const std::string file = board->file;
    auto remove = [this, file, next] {
        for (const auto& b : m_config.boards())
            if (b->file == file) {
                if (m_placements->board() == b.get()) m_placements->setBoard(nullptr);
                m_config.removeBoard(b.get());
                break;
            }
        next();
    };
    if (board->dirty && confirmSave) confirmSave(*board, remove);
    else remove();
}

void JPBoardsPanel::copyBoard() {
    JPBoard* source = selection();
    if (!source) return;
    const std::string sourceFile = source->file;
    JDialog::saveFile("Save Copy of Board As...", { "xml" }, [this, sourceFile](std::string chosen) {
        const JPBoard* from = nullptr;
        for (const auto& b : m_config.boards())
            if (b->file == sourceFile) from = b.get();
        if (!from) return;
        const std::string path = JPConfiguration::canonical(withBoardSuffix(chosen));
        auto copy = std::make_shared<JPBoard>(*from);
        copy->makeDefinition();
        copy->file = path;
        copy->name = std::filesystem::path(path).filename().string();
        copy->dirty = false;
        std::string error;
        if (!m_config.saveBoard(*copy, error)) {
            JDialog::message("Unable to create copy of board", error);
            return;
        }
        m_config.addBoard(copy);
        m_table->refresh();
        selectBoard(copy.get());
        changed();
    });
}

} // inline namespace jf
