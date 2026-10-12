// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// A placement whose library part was deleted shows what the files called its part, not the id the deleted part
// had (a GUID that tells nothing): what the files said is kept with the board's part, and the Part cell's
// tooltip says all of it. One not chosen yet shows its name the same way.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "ui/JPPlacementsTableModel.h"

#include <filesystem>

using namespace jf;
namespace fs = std::filesystem;

int main() {
    const fs::path dir = fs::temp_directory_path() / "jplacer-test-deleted-part-shown";
    fs::remove_all(dir);
    fs::create_directories(dir);
    JPConfiguration config(dir.string());
    auto part = std::make_shared<JPPart>();
    part->id = "0493a50b-598b-4ae5-8eb9-00200796898e";
    part->label = "R0603-10k";
    config.addPart(part);
    std::string error;
    auto board = config.board((dir / "b.jpboard").string(), error);
    assert(board);

    JPBoardPart matched;
    matched.key = "bp-1";
    matched.fields = { { "part", "R_0603-10k" }, { "value", "10k" }, { "footprint", "R_0603_1608Metric" }, { "mpn", "RC0603FR-0710KL" } };
    matched.state = JPBoardPart::State::Matched;
    matched.libraryPartId = part->id;
    JPBoardPart unmatched;
    unmatched.key = "bp-2";
    unmatched.fields = { { "value", "100n" }, { "footprint", "C_0402" } };
    board->parts() = { matched, unmatched };
    JPPlacement r1;
    r1.id = "R1";
    r1.boardPart = "bp-1";
    r1.partId = part->id;
    JPPlacement c1;
    c1.id = "C1";
    c1.boardPart = "bp-2";
    c1.partId = "100n";
    board->placements = { r1, c1 };

    JPPlacementsTableModel model(config, [] { return static_cast<const JPJob*>(nullptr); },
                                 { JPPlacementsTableModel::kId, JPPlacementsTableModel::kPart });
    model.setHolder(board.get());
    const int rr = model.rowOf("R1"), rc = model.rowOf("C1");
    assert(model.text(rr, 1) == "R0603-10k" && model.cellTooltip(rr, 1).empty());

    // The library's part deleted: the files' name, and all they said in the tooltip.
    config.removePart(part->id);
    assert(model.text(rr, 1) == "R_0603-10k (not in the library)");
    const std::string tip = model.cellTooltip(rr, 1);
    assert(tip.find("deleted") != std::string::npos && tip.find("RC0603FR-0710KL") != std::string::npos
           && tip.find("10k") != std::string::npos);
    assert(model.status(*model.placement(rr)) == JPPlacementsTableModel::Status::MissingPart);

    // Not chosen yet, no part name in the files: its value and footprint.
    assert(model.text(rc, 1) == "100n C_0402 (to be chosen)");

    fs::remove_all(dir);
    return 0;
}
