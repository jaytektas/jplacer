// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// A panel's children changed as the Panels tab changes them: replaced (its
// alignment pseudo-placements kept while they still name something),
// removed (theirs going with them), added and edited on the panel and on
// each use of it.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "model/JPBoardLocation.h"
#include "model/JPConfiguration.h"
#include "model/JPDefinitionChanges.h"
#include "model/JPPanelLocation.h"

#include <filesystem>

using namespace jf;
namespace fs = std::filesystem;

int main() {
    const fs::path dir = fs::temp_directory_path() / "jplacer-test-panel-edits";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const fs::path data = fs::path(JPLACER_TESTDATA_DIR) / "openpnp" / "pnp-test";
    fs::copy_file(data / "pnp-test.board.xml", dir / "pnp-test.board.xml");
    fs::copy_file(data / "pnp-test.panel.xml", dir / "pnp-test.panel.xml");

    JPConfiguration config(dir.string());
    std::string error;
    const auto panel = config.panel((dir / "pnp-test.panel.xml").string(), error);
    assert(panel && panel->children.size() == 3 && panel->pseudoPlacementIds.size() == 4);
    const auto board = config.board((dir / "pnp-test.board.xml").string(), error);
    assert(board);

    // A second panel holding the first: a use of it, to be kept in step.
    auto outer = std::make_shared<JPPanel>();
    outer->file = (dir / "outer.panel.xml").string();
    auto use = std::make_unique<JPPanelLocation>();
    use->holder = panel->instance();
    outer->addChild(std::move(use));
    config.addPanel(outer);
    assert(config.instancesOf(*panel, nullptr).size() == 1);
    auto* inner = static_cast<JPPanel*>(config.instancesOf(*panel, nullptr).front());

    JPDefinitionChanges changes(config, nullptr);
    // Replaced by the same board: Brd1's pseudo-placements still name something.
    JPBoardLocation replacement;
    replacement.holder = board->instance();
    changes.childReplaced(*panel, "Brd1", replacement);
    assert(panel->child("Brd1") && panel->child("Brd1")->location().x() == 3.0);
    assert(panel->pseudoPlacementIds.size() == 4);
    assert(inner->child("Brd1")->definition() == panel->child("Brd1"));

    // An edit to a child reaches the use.
    changes.child(*panel, "Brd2", [](JPPlacementsHolderLocation& c) { c.checkFiducials = false; });
    assert(!panel->child("Brd2")->checkFiducials && !inner->child("Brd2")->checkFiducials);

    // Added: given an id, and to the use.
    auto added = std::make_unique<JPBoardLocation>();
    added->holder = board->instance();
    const JPPlacementsHolderLocation* a = changes.childAdded(*panel, std::move(added));
    assert(a->id == "Brd4" && inner->child("Brd4"));

    // Removed: Brd1 and its two pseudo-placements, from the panel and the use.
    changes.childRemoved(*panel, "Brd1");
    assert(!panel->child("Brd1") && !inner->child("Brd1") && panel->pseudoPlacementIds.size() == 2);
    changes.pseudoPlacementsChanged(*panel);
    assert(inner->pseudoPlacementIds == panel->pseudoPlacementIds && panel->dirty);

    // A pseudo-placement turned off shows so.
    panel->disabledPseudoPlacements.insert(panel->pseudoPlacementIds.front());
    assert(!panel->pseudoPlacements().front().enabled);

    fs::remove_all(dir);
    return 0;
}
