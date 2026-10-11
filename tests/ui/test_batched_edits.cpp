// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// A change to many placements at once (all of a board's chosen and disabled) is one change: applied to each,
// then said once. Said once a placement, each saved the configuration and had everything listening work its
// answer out again, and disabling a few hundred placements froze the app.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "ui/JPPlacementsTableModel.h"

#include <filesystem>

using namespace jf;
namespace fs = std::filesystem;

int main() {
    const fs::path dir = fs::temp_directory_path() / "jplacer-test-batched-edits";
    fs::remove_all(dir);
    fs::create_directories(dir);
    JPConfiguration config(dir.string());
    std::string error;
    auto board = config.board((dir / "b.jpboard").string(), error);
    assert(board);
    std::vector<std::string> ids;
    for (int i = 1; i <= 300; ++i) {
        JPPlacement p;
        p.id = "R" + std::to_string(i);
        board->placements.push_back(p);
        ids.push_back(p.id);
    }
    JPPlacementsTableModel model(config, [] { return static_cast<const JPJob*>(nullptr); },
                                 { JPPlacementsTableModel::kEnabled, JPPlacementsTableModel::kId });
    model.setHolder(board.get());
    int said = 0;
    model.onChanged = [&said] { ++said; };

    model.editEach(ids, [](JPPlacement& p) { p.enabled = false; });
    assert(said == 1);
    for (const JPPlacement& p : board->placements) assert(!p.enabled);
    assert(board->dirty);

    // One placement: one change, as before; none chosen: nothing said.
    model.edit("R7", [](JPPlacement& p) { p.enabled = true; });
    assert(said == 2 && board->find("R7")->enabled);
    model.editEach({}, [](JPPlacement& p) { p.enabled = true; });
    assert(said == 2);

    fs::remove_all(dir);
    return 0;
}
