// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPBoardPlacementsPanel.h"

#include "JPUiParts.h"

#include "model/JPDefinitionChanges.h"
#include "model/JPSides.h"

#include <j/core/Dialog.h>
#include <j/core/JLabel.h>
#include <j/core/JSeparator.h>
#include <j/core/JStyle.h>
#include <j/core/JTextHelper.h>

#include <algorithm>

inline namespace jf {

namespace {

// OpenPnP's search box is fifteen characters wide.
constexpr int kSearchColumns = 15;

std::unique_ptr<JSeparator> toolSeparator(JSceneGraph& graph) {
    return std::make_unique<JSeparator>(graph, JSeparator::JOrientation::Vertical, JPIconButton::size());
}

std::string trimmed(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

} // namespace

JPBoardPlacementsPanel::JPBoardPlacementsPanel(JSceneGraph& graph, JPConfiguration& config,
                                               std::function<const JPJob*()> job)
    : JContainer(graph, 0.f, 0.f)
    , m_config(config)
    , m_job(job)
    , m_model(config, job,
              { JPPlacementsTableModel::kEnabled, JPPlacementsTableModel::kId, JPPlacementsTableModel::kPart,
                JPPlacementsTableModel::kSide, JPPlacementsTableModel::kX, JPPlacementsTableModel::kY,
                JPPlacementsTableModel::kRotation, JPPlacementsTableModel::kType,
                JPPlacementsTableModel::kErrorHandling, JPPlacementsTableModel::kRank,
                JPPlacementsTableModel::kComments })
    , m_importers(JPBoardImporter::all()) {
    setDirection(JFlexDirection::Column)->setAlignItems(JAlignItems::Stretch);
    const JStyle& st = JStyle::current();
    m_model.onChanged = [this] { changed(); };

    auto bar = JPUiParts::row(graph);
    auto tool = [&](const char* name, const char* icon, const char* tip) {
        return bar->add(std::make_unique<JPIconButton>(graph, name, icon, tip));
    };
    m_new = tool("New Placement", "general-add", "Create a new placement and add it to the board.");
    m_new->setLeads(JPIconButton::Leads::Elsewhere);
    m_new->onClicked.connect([this] { newPlacement(); });
    m_remove = tool("Remove Placement(s)", "general-remove", "Remove the currently selected placement(s).");
    m_remove->onClicked.connect([this] { removePlacements(); });
    bar->add(toolSeparator(graph));
    m_import = tool("Import Placements", "import", "Import placements from CAD files");
    m_import->setLeads(JPIconButton::Leads::Menu);
    m_import->onClicked.connect([this] { showImportMenu(); });
    m_parts = tool("Board's Parts", "footprint-dual", "The board's parts, one each: what the files said, what it is, the "
                                                      "library's best match; choose them");
    m_parts->setLeads(JPIconButton::Leads::Elsewhere);
    m_parts->onClicked.connect([this] {
        if (m_board && openBoardParts) openBoardParts(*m_board);
    });
    bar->add(toolSeparator(graph));
    m_view = tool("View Board", "color-true", "View a graphical representation of the selected board.");
    m_view->setLeads(JPIconButton::Leads::Elsewhere);
    m_view->onClicked.connect([this] {
        if (m_board && onViewBoard) onViewBoard();
    });
    bar->add(std::make_unique<JContainer>(graph, 0.f, 0.f))->setHSizePolicy(JSizePolicyMode::Expanding, 1);
    JLabel* searchLabel = bar->add(std::make_unique<JLabel>(graph, "Search"));
    searchLabel->setFixedSize(JTextHelper::measureWidth("Search") + st.spacing, st.controlHeight);
    m_search = bar->add(std::make_unique<JLineEdit>(graph, ""));
    m_search->setFixedSize(JTextHelper::measureWidth("M") * kSearchColumns, st.controlHeight);
    m_search->onTextChanged.connect([this](const std::string& t) { m_table->setFilter(trimmed(t)); });
    add(std::move(bar));

    m_table = add(std::make_unique<JPTable>(graph));
    m_table->setModel(&m_model);
    m_table->openMenu = [this](JMenu* m, float x, float y) { if (openMenu) openMenu(m, x, y); };
    m_table->onSelectionChanged.connect([this] {
        updateActions();
        const auto s = selections();
        if (s.size() <= 1 && onPlacementChosen) onPlacementChosen(s.empty() ? nullptr : s.front());
    });
    m_table->onEditRefused = [](const std::string&) {};
    // Space turns the (first) chosen placement on or off.
    m_table->onKey = [this](const JKeyEvent& ke) {
        if (ke.key != JKeyEvent::JKey::Space || ke.ctrl || ke.alt) return false;
        const auto chosen = selections();
        if (chosen.empty()) return false;
        const bool on = !chosen.front()->enabled;
        m_model.edit(chosen.front()->id, [on](JPPlacement& p) { p.enabled = on; });
        m_table->refresh();
        return true;
    };
    buildContextMenu();
    m_table->setContextMenu(m_contextMenu.get());
    m_table->onContextMenu = [this](int) { updateActions(); };
    setBoard(nullptr);
}

void JPBoardPlacementsPanel::buildContextMenu() {
    JSceneGraph& g = m_graph;
    m_contextMenu = std::make_unique<JMenu>("Placements");
    // OpenPnP's descriptions as tooltips: the submenu's, and each value's ("Set placement side(s) to Top").
    auto sub = [&](const std::string& title, const std::string& tip) {
        m_subMenus.push_back(std::make_unique<JMenu>(title));
        m_contextMenu->add(g, title, {}, m_subMenus.back().get())->setTooltip(tip + "...");
        return m_subMenus.back().get();
    };
    auto value = [&](JMenu* on, const std::string& name, const std::string& tip) {
        JMenuItem* item = on->add(g, name);
        item->setTooltip(tip + " " + name);
        return item;
    };
    auto forChosen = [this](std::function<void(JPPlacement&)> set) {
        return [this, set] {
            for (const JPPlacement* p : selections()) m_model.edit(p->id, set);
            m_table->refresh();
        };
    };
    JMenu* type = sub("Set Type", "Set placement type(s) to");
    for (JPPlacement::Type t : { JPPlacement::Type::Placement, JPPlacement::Type::Fiducial })
        value(type, JPPlacement::typeName(t), "Set placement type(s) to")->onTriggered.connect(forChosen([t](JPPlacement& p) { p.type = t; }));
    JMenu* side = sub("Set Side", "Set placement side(s) to");
    for (JPSide s : { JPSide::Bottom, JPSide::Top })
        value(side, JPSides::name(s), "Set placement side(s) to")->onTriggered.connect(forChosen([s](JPPlacement& p) { p.side = s; }));
    JMenu* enabled = sub("Set Enabled", "Set placement enable(s) to");
    for (bool on : { true, false })
        value(enabled, on ? "Enabled" : "Disabled", "Set placement enable(s) to")->onTriggered.connect(forChosen([on](JPPlacement& p) { p.enabled = on; }));
    JMenu* errors = sub("Set Error Handling", "Set placement error handling(s) to");
    for (JPPlacement::ErrorHandling e :
         { JPPlacement::ErrorHandling::Default, JPPlacement::ErrorHandling::Alert, JPPlacement::ErrorHandling::Defer })
        value(errors, JPPlacement::errorHandlingName(e), "Set placement error handling(s) to")
            ->onTriggered.connect(forChosen([e](JPPlacement& p) { p.errorHandling = e; }));
}

void JPBoardPlacementsPanel::setBoard(JPBoard* board) {
    m_board = board;
    m_model.setHolder(board);
    m_table->refresh();
    updateActions();
}

void JPBoardPlacementsPanel::refresh() {
    m_table->refresh();
    updateActions();
}

void JPBoardPlacementsPanel::selectPlacement(const std::string& id) {
    m_table->selectRow(id.empty() ? -1 : m_model.rowOf(id));
}

std::vector<JPPlacement*> JPBoardPlacementsPanel::selections() const {
    std::vector<JPPlacement*> out;
    for (const int r : m_table->selectedRows())
        if (JPPlacement* p = m_model.placement(r)) out.push_back(p);
    return out;
}

void JPBoardPlacementsPanel::updateActions() {
    const bool any = !selections().empty();
    m_new->setEnabled(m_board != nullptr);
    m_import->setEnabled(m_board != nullptr);
    m_view->setEnabled(m_board != nullptr);
    // As OpenPnP's action groups: removing and the right-click menu's entries want a placement chosen.
    m_remove->setEnabled(any);
    for (size_t i = 0; i < m_contextMenu->items().size(); ++i) m_contextMenu->items()[i]->setEnabled(any);
}

void JPBoardPlacementsPanel::changed() {
    if (onChanged) onChanged();
}

void JPBoardPlacementsPanel::newPlacement() {
    if (!m_board) return;
    if (m_config.parts().empty()) {
        JDialog::message("Error", "There are currently no parts defined in the system. Please create at least one "
                                  "part before creating a placement.");
        return;
    }
    JPBoard* board = m_board;
    JDialog::input("Input", "Please enter an ID for the new placement.", [this, board](std::string text) {
        const std::string id = trimmed(text);
        if (id.empty() || board != m_board) return;
        if (board->find(id)) {
            JDialog::message("Error", "The ID for the new placement already exists");
            return;
        }
        JPPlacement p;
        p.id = id;
        // As OpenPnP: the library's first part, as one of the board's parts.
        if (!m_config.parts().empty()) {
            p.partId = m_config.parts().front()->id;
            p.boardPart = board->useLibraryPart(p.partId);
        }
        p.location = JPLocation(JPLengthUnit::Millimeters);
        p.side = JPSide::Top;
        JPDefinitionChanges(m_config, m_job()).added(*board, p);
        m_table->refresh();
        // As OpenPnP: the last row shown is chosen.
        m_table->selectRow(m_table->modelRowAt(m_table->viewRowCount() - 1));
        changed();
    });
}

void JPBoardPlacementsPanel::removePlacements() {
    if (!m_board) return;
    std::vector<std::string> ids;
    for (const JPPlacement* p : selections()) ids.push_back(p->id);
    JPDefinitionChanges changes(m_config, m_job());
    for (const std::string& id : ids) changes.removed(*m_board, id);
    m_table->refresh();
    changed();
}

void JPBoardPlacementsPanel::showImportMenu() {
    if (!openMenu) return;
    m_importMenu = std::make_unique<JMenu>("Import Placements");
    m_importMenu->add(m_graph, "CPL and BOM…")->onTriggered.connect([this] { importCplBom(); });
    m_importMenu->addSeparator(m_graph);
    for (const auto& importer : m_importers) {
        const JPBoardImporter* i = importer.get();
        m_importMenu->add(m_graph, i->name())->onTriggered.connect([this, i] { importBoard(*i); });
    }
    const JRect b = m_graph.getLayoutConst(m_import->getNodeId()).boundingBox;
    openMenu(m_importMenu.get(), b.x, b.y + b.height);
}

void JPBoardPlacementsPanel::importBoard(const JPBoardImporter& importer) {
    if (!m_board) {
        JDialog::message("Import Failed", "Please select a board to import into.");
        return;
    }
    if (!openImporter) return;
    JPBoard* board = m_board;
    openImporter(importer, [this, board](JPBoard& imported) {
        // What the import was asked to change in the library (Update Existing Part Heights, Eagle's Update
        // Existing Parts) is changed, whatever comes next; its parts are the board's, taken in merge().
        changed();
        take(board, imported);
    });
}

void JPBoardPlacementsPanel::importCplBom() {
    if (!m_board) {
        JDialog::message("Import Failed", "Please select a board to import into.");
        return;
    }
    if (!openCplBom) return;
    JPBoard* board = m_board;
    openCplBom([this, board](JPBoard& imported) {
        // Parts left to choose: their list once it is merged (after Merge or Replace, when that is asked).
        m_partsAfterMerge = std::any_of(imported.parts().begin(), imported.parts().end(),
                                        [](const JPBoardPart& p) { return p.state == JPBoardPart::State::Unmatched; });
        take(board, imported);
    });
}

void JPBoardPlacementsPanel::take(JPBoard* board, JPBoard& imported) {
    if (board != m_board) return;
    auto shared = std::make_shared<JPBoard>(imported);
    if (board->placements.empty() || !askChoice) {
        merge(*shared);
        return;
    }
    askChoice("The Selected Board Already Has Existing Placements",
              "What do you want to do?\n\nSelect Merge to update the existing placements whose IDs match those in "
              "the imported set, leave unchanged the existing placements whose IDs do not match any in the "
              "imported set, and add the imported placements whose IDs do not match any of the existing "
              "placements.\n\nSelect Replace to delete all existing placements and then add all the imported "
              "placements.",
              { "Merge", "Replace", "Cancel" }, 2, [this, board, shared](int option) {
                  if (board != m_board || option < 0 || option == 2) {
                      m_partsAfterMerge = false;
                      return;
                  }
                  if (option == 1) {
                      JPDefinitionChanges changes(m_config, m_job());
                      std::vector<std::string> ids;
                      for (const JPPlacement& p : board->placements) ids.push_back(p.id);
                      for (const std::string& id : ids) changes.removed(*board, id);
                  }
                  merge(*shared);
              });
}

void JPBoardPlacementsPanel::merge(JPBoard& imported) {
    if (!m_board) return;
    JPDefinitionChanges changes(m_config, m_job());
    // The imported board's parts become this board's (those it has kept as they are), then its placements.
    const std::map<std::string, std::string> keys = m_board->takeParts(imported);
    auto partOf = [this, &keys](JPPlacement p) {
        if (const auto k = keys.find(p.boardPart); k != keys.end()) p.boardPart = k->second;
        if (const JPBoardPart* bp = m_board->part(p.boardPart)) p.partId = bp->partId();
        return p;
    };
    for (const JPPlacement& read : imported.placements) {
        const JPPlacement p = partOf(read);
        if (m_board->find(p.id)) {
            // Merged: the part, side, location and comments taken from the file.
            changes.placement(*m_board, p.id, [&p](JPPlacement& q) {
                q.boardPart = p.boardPart;
                q.partId = p.partId;
                q.side = p.side;
                q.location = p.location;
                q.comments = p.comments;
            });
        } else {
            changes.added(*m_board, p);
        }
    }
    m_board->dropUnusedParts();
    // The files it came from, kept with the board.
    for (const JJson& source : imported.provenance) m_board->provenance.push_back(source);
    if (m_partsAfterMerge) {
        m_partsAfterMerge = false;
        if (openBoardParts) openBoardParts(*m_board);
    }
    // Paste pads in the board's units, as OpenPnP puts them.
    for (JPBoardPad pad : imported.solderPastePads) {
        pad.location = pad.location.convertToUnits(m_board->dimensions.units());
        m_board->solderPastePads.push_back(pad);
    }
    m_board->dirty = true;
    m_table->refresh();
    changed();
}

} // inline namespace jf
