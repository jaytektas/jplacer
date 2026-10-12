// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPBoardTools.h"

#include "JPUiParts.h"

#include "model/JPDefinitionChanges.h"
#include "model/JPSides.h"

#include "common/JPlacerLog.h"

#include <j/core/Dialog.h>
#include <j/core/Log.h>
#include <j/core/JLabel.h>
#include <j/core/JSeparator.h>
#include <j/core/JStyle.h>
#include <j/core/JTextHelper.h>

#include <algorithm>

inline namespace jf {

namespace {

constexpr int kRevisionColumns = 8;      // the revision chooser's width, in Ms
constexpr size_t kCarryListed = 12;      // designators named in the work given, before "and n more"

std::unique_ptr<JSeparator> toolSeparator(JSceneGraph& graph) {
    return std::make_unique<JSeparator>(graph, JSeparator::JOrientation::Vertical, JPIconButton::size());
}


} // namespace

JPBoardTools::JPBoardTools(JSceneGraph& graph, JPConfiguration& config, std::function<const JPJob*()> job)
    : JContainer(graph, 0.f, 0.f), m_config(config), m_job(job), m_importers(JPBoardImporter::all()) {
    const JStyle& st = JStyle::current();
    setDirection(JFlexDirection::JRow)->setAlignItems(JAlignItems::Center)->setGap(st.spacing);
    auto tool = [&](const char* name, const char* icon, const char* tip) {
        return add(std::make_unique<JPIconButton>(graph, name, icon, tip));
    };
    m_import = tool("Import Placements", "import", "Import placements from CAD files");
    m_import->setLeads(JPIconButton::Leads::Menu);
    m_import->onClicked.connect([this] { showImportMenu(); });
    m_parts = tool("Board's Parts", "footprint-dual", "The board's parts, one each: what the files said, what it is, the "
                                                      "library's best match; choose them");
    m_parts->setLeads(JPIconButton::Leads::Elsewhere);
    m_parts->onClicked.connect([this] {
        if (m_board && openBoardParts) openBoardParts(*m_board);
    });
    m_view = tool("View Board", "color-true", "View a graphical representation of the selected board.");
    m_view->setLeads(JPIconButton::Leads::Elsewhere);
    m_view->onClicked.connect([this] {
        if (m_board && onViewBoard) onViewBoard();
    });
    add(toolSeparator(graph));
    JLabel* revisionLabel = add(std::make_unique<JLabel>(graph, "Revision"));
    revisionLabel->setFixedSize(JTextHelper::measureWidth("Revision") + st.spacing, st.controlHeight);
    m_revision = add(std::make_unique<JComboBox>(graph, std::vector<std::string> {}));
    m_revision->setFixedSize(JTextHelper::measureWidth("M") * kRevisionColumns, st.controlHeight);
    m_revision->onIndexChanged.connect([this](int i) {
        if (!m_fillingRevisions) switchRevision(i);
    });
    setBoard(nullptr);
}

void JPBoardTools::setBoard(JPBoard* board) {
    m_board = board;
    refresh();
}

void JPBoardTools::refresh() {
    fillRevisions();
    for (JPIconButton* b : { m_import, m_parts, m_view }) b->setEnabled(m_board != nullptr);
}

void JPBoardTools::fillRevisions() {
    m_fillingRevisions = true;
    std::vector<std::string> labels;
    if (m_board)
        for (const JPBoardRevision& r : m_board->revisions()) labels.push_back(r.label);
    const bool any = !labels.empty();
    if (!any) labels.push_back("None yet");
    m_revision->setItems(labels);
    m_revision->setCurrentIndex(any ? int(m_board->revision()) : 0);
    m_revision->setEnabled(any);
    m_revision->setTooltip(any ? "The board's revision shown, here and in the job: each kept as it was left"
                               : "The board keeps no revisions yet: import a new revision's files (Import Placements) "
                                 "and choose New Revision");
    m_fillingRevisions = false;
}

void JPBoardTools::switchRevision(int index) {
    if (!m_board || index < 0 || size_t(index) == m_board->revision()) return;
    JPBoard* board = m_board;
    const std::string to = board->revisions()[size_t(index)].label;
    auto carry = std::make_shared<JPRevisionCarry>(board->switchRevision(size_t(index)));
    JPDefinitionChanges(m_config, m_job()).revisionShown(*board);
    JLOGC(JPlacerLog::kUi, JLogLevel::Info) << "board " << board->scopeName() << ": " << to << " shown";
    refresh();
    changed();
    if (carry->empty() || !askChoice) return;
    // The work given, shown, to keep or undo.
    auto list = [](const std::vector<std::string>& ids) {
        std::string s;
        for (size_t i = 0; i < ids.size() && i < kCarryListed; ++i) s += (i ? ", " : "") + ids[i];
        return ids.size() > kCarryListed ? s + " and " + std::to_string(ids.size() - kCarryListed) + " more" : s;
    };
    std::string said = "Placements the same in " + to + " as in " + carry->from + " (where the CAD puts and turns "
                       "them, and their part) were given the work done on " + carry->from + ":\n";
    if (!carry->rotations.empty()) said += "\nRotation verified there: " + list(carry->rotations);
    if (!carry->verified.empty()) said += "\nVerified there: " + list(carry->verified);
    if (!carry->parts.empty()) said += "\nPart chosen as there: " + list(carry->parts);
    askChoice("Work Given from " + carry->from, said, { "Keep", "Undo" }, 0, [this, board, carry](int option) {
        if (option != 1 || board != m_board) return;
        board->undoCarry(*carry);
        JPDefinitionChanges(m_config, m_job()).revisionShown(*board);
        refresh();
        changed();
    });
}




void JPBoardTools::changed() {
    if (onChanged) onChanged();
}



void JPBoardTools::showImportMenu() {
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

void JPBoardTools::importBoard(const JPBoardImporter& importer) {
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

void JPBoardTools::importCplBom() {
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

void JPBoardTools::take(JPBoard* board, JPBoard& imported) {
    if (board != m_board) return;
    auto shared = std::make_shared<JPBoard>(imported);
    if (board->placements.empty() || !askChoice) {
        merge(*shared);
        return;
    }
    std::vector<std::string> options { "Merge", "Replace" };
    std::string question = "What do you want to do?\n\nSelect Merge to update the existing placements whose IDs match "
                           "those in the imported set, leave unchanged the existing placements whose IDs do not match "
                           "any in the imported set, and add the imported placements whose IDs do not match any of "
                           "the existing placements.\n\nSelect Replace to delete all existing placements and then add "
                           "all the imported placements.";
    if (openUpgrade) {
        options.push_back("New Revision…");
        question += "\n\nSelect New Revision to make the imported set the board's next revision: what changed is "
                    "shown first, decisions that still hold are kept, and the revision shown now is kept to switch "
                    "back to.";
    }
    options.push_back("Cancel");
    const int cancel = int(options.size()) - 1;
    askChoice("The Selected Board Already Has Existing Placements", question, options, cancel,
              [this, board, shared, cancel](int option) {
                  if (board != m_board || option < 0 || option == cancel) {
                      m_partsAfterMerge = false;
                      return;
                  }
                  if (option == 2) {
                      m_partsAfterMerge = false;
                      upgrade(board, shared);
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

void JPBoardTools::takeMade(const JPBoard& imported) {
    int packages = 0;
    const int parts = m_config.takeMade(imported, packages);
    if (parts > 0 || packages > 0)
        JLOGC(JPlacerLog::kBoardImport, JLogLevel::Info) << parts << " part(s) and " << packages
                                                         << " package(s) the import made added to the library";
    if (onImportTaken) onImportTaken(imported);
}

void JPBoardTools::upgrade(JPBoard* board, std::shared_ptr<JPBoard> files) {
    openUpgrade(*board, files, [this, board, files](JPBoardRevision r, std::string current) {
        if (board != m_board) return;
        takeMade(*files);   // the parts it made the library's, as the revision is made
        board->addRevision(std::move(r), current);
        JPDefinitionChanges(m_config, m_job()).revisionShown(*board);
        // The board's fields added: the files' where it has no value yet.
        JPDefinitionChanges(m_config, m_job()).holder(*board, [&files](JPPlacementsHolder& h) {
            for (const auto& [key, v] : files->custom.all())
                if (!h.custom.has(key)) h.custom.set(key, v);
        });
        JLOGC(JPlacerLog::kUi, JLogLevel::Info)
            << "board " << board->scopeName() << ": " << board->revisionLabel() << " made: " << board->revisions().back().summary;
        refresh();
        changed();
        // Parts left to choose: the board's parts.
        if (openBoardParts && std::any_of(board->parts().begin(), board->parts().end(), [](const JPBoardPart& p) {
                return p.state == JPBoardPart::State::Unmatched;
            }))
            openBoardParts(*board);
    });
}

void JPBoardTools::merge(JPBoard& imported) {
    if (!m_board) return;
    takeMade(imported);   // the parts and packages it made, the library's
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
                // The fields added: the file's where the placement has no value yet (one set by hand is kept).
                for (const auto& [key, v] : p.custom.all())
                    if (!q.custom.has(key)) q.custom.set(key, v);
            });
        } else {
            changes.added(*m_board, p);
        }
    }
    m_board->dropUnusedParts();
    // The board's fields added: the files' where it has no value yet.
    changes.holder(*m_board, [&imported](JPPlacementsHolder& h) {
        for (const auto& [key, v] : imported.custom.all())
            if (!h.custom.has(key)) h.custom.set(key, v);
    });
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
    changed();
}

} // inline namespace jf
