// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// A panel's children name their files: one added or replaced in the panel's definition is saved with its
// board's (or panel's) file, and a panel saved before that, its children naming none, still opens (those
// children left out: nothing names what they were) rather than its folder being read as a board.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "model/JPConfiguration.h"
#include "model/JPDefinitionChanges.h"

#include <filesystem>
#include <fstream>

using namespace jf;
namespace fs = std::filesystem;

int main() {
    const fs::path dir = fs::temp_directory_path() / "jplacer-test-panel-child-files";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const std::string boardFile = (dir / "a.jpboard").string();
    const std::string panelFile = (dir / "p.jppanel").string();

    // A board and a panel added to it in its definition: the child saved naming the board's file.
    {
        JPConfiguration config((dir / "config").string());
        std::string error;
        auto board = config.board(boardFile, error);
        auto panel = config.panel(panelFile, error);
        assert(board && panel);
        auto l = std::make_unique<JPBoardLocation>();
        l->holder = board->instance();
        JPPlacementsHolderLocation* added = JPDefinitionChanges(config, nullptr).childAdded(*panel, std::move(l));
        assert(added->fileName == board->file);
        assert(config.savePanel(*panel, error));

        JPConfiguration again((dir / "config").string());
        auto reread = again.panel(panelFile, error);
        assert(reread && reread->children.size() == 1 && reread->children[0]->fileName == board->file);

        // Replaced: the replacement's file.
        const std::string otherFile = (dir / "b.jpboard").string();
        auto other = config.board(otherFile, error);
        JPBoardLocation with;
        with.holder = other->instance();
        JPDefinitionChanges(config, nullptr).childReplaced(*panel, added->id, with);
        assert(panel->children.size() == 1 && panel->children[0]->fileName == other->file);
    }

    // A panel saved naming no file for its boards (as the panel's definition saved them before), in a job
    // beside it: the job opens, the panel without them.
    {
        std::ofstream(panelFile) << R"({ "format": "jplacer-panel", "version": 1, "name": "p.jppanel",
            "dimensions": { "units": "Millimeters", "x": 0, "y": 0, "z": 0, "rotation": 0 }, "placements": [],
            "children": [ { "kind": "Board", "id": "Brd1", "file": "", "side": "Top", "checkFiducials": false,
                            "enabled": true, "location": { "units": "Millimeters", "x": 0, "y": 0, "z": 0, "rotation": 0 } } ],
            "pseudoPlacements": [] })";
        const std::string jobFile = (dir / "j.jpjob").string();
        std::ofstream(jobFile) << R"({ "format": "jplacer-job", "version": 1, "root": {
            "dimensions": { "units": "Millimeters", "x": 0, "y": 0, "z": 0, "rotation": 0 }, "placements": [],
            "children": [ { "kind": "Panel", "id": "Pnl1", "file": ")" << panelFile << R"(", "side": "Top",
                            "checkFiducials": false, "enabled": true,
                            "location": { "units": "Millimeters", "x": 0, "y": 0, "z": 0, "rotation": 0 } } ],
            "pseudoPlacements": [] } })";
        JPConfiguration config((dir / "config").string());
        std::string error;
        auto job = config.loadJob(jobFile, error);
        assert(job);
        auto panel = config.panel(panelFile, error);
        assert(panel && panel->children.empty() && panel->dirty);
    }

    // A folder is not a board: none found, nothing read.
    {
        JPConfiguration config((dir / "config").string());
        std::string error;
        JPBoardLocation l;
        l.fileName = "";
        assert(!config.resolveBoard(nullptr, l, error) && !error.empty());
    }

    fs::remove_all(dir);
    return 0;
}
