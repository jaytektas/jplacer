// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// A board carries a copy of each library part it uses (DESIGN.md, Board part): taken when the part is
// chosen, kept in its file; the library's part changed since (not merely learning a name) is told apart by
// the copy's fingerprint, and either side can take the other's; renamed, the part is found by its uuid; on a
// machine whose library lacks it, the board places with its copy.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>
#include <cmath>

#include "model/JPConfiguration.h"

#include <filesystem>

using namespace jf;
namespace fs = std::filesystem;

int main() {
    const fs::path dir = fs::temp_directory_path() / "jplacer-test-board-copies";
    fs::remove_all(dir);
    fs::create_directories(dir / "other");
    JPConfiguration config(dir.string());
    auto pkg = std::make_shared<JPPackage>();
    pkg->id = "C0603";
    config.addPackage(pkg);
    auto cap = std::make_shared<JPPart>();
    cap->id = "C0603-100n";
    cap->packageId = "C0603";
    cap->height = JPLength(0.8, JPLengthUnit::Millimeters);
    config.addPart(cap);

    JPBoardPart bp;
    bp.key = "bp-1";
    bp.state = JPBoardPart::State::Matched;
    bp.libraryPartId = "C0603-100n";
    config.takeCopy(bp);
    assert(bp.copyPart && bp.copyPackage && bp.libraryUuid == cap->uuid && !bp.fingerprint.empty() && !config.differs(bp));

    // Learning a name is not a change; a height is.
    cap->akas.push_back({ "value", "100nF", "Ctrl", "now" });
    assert(!config.differs(bp));
    cap->height = JPLength(0.9, JPLengthUnit::Millimeters);
    assert(config.differs(bp));
    // An offer is not a change either; how it comes (its rotation in the tape) is.
    cap->height = JPLength(0.8, JPLengthUnit::Millimeters);
    cap->offers.push_back({ "LCSC", "C14663", "Reel", 4000, "", "", "", "" });
    assert(!config.differs(bp));
    cap->packagings.push_back({ "Reel", 8, 4, "Paper", 90, 4000, "" });
    assert(config.differs(bp));
    cap->packagings.clear();
    cap->height = JPLength(0.9, JPLengthUnit::Millimeters);
    // The library takes the board's: the height back, its names kept.
    config.giveCopy(bp);
    assert(std::abs(cap->height.value() - 0.8) < 1e-9 && cap->akas.size() == 1 && !config.differs(bp));
    // The board takes the library's.
    cap->height = JPLength(1.0, JPLengthUnit::Millimeters);
    config.takeCopy(bp);
    assert(!config.differs(bp) && std::abs(bp.copyPart->height.value() - 1.0) < 1e-9);
    // Renamed: found by its uuid.
    cap->id = "Cap 100n";
    config.removePart("C0603-100n");
    config.addPart(cap);
    assert(config.libraryPartFor(bp) == cap.get());

    // The board's file carries it; on another machine, with an empty library, the board places with its copy.
    JPBoard board;
    board.parts().push_back(bp);
    JPPlacement c1;
    c1.id = "C1";
    c1.boardPart = "bp-1";
    board.placements.push_back(c1);
    board.syncParts();
    const std::string file = (dir / "other" / "ctrl.jpboard").string();
    assert(board.toJson().dumpToFile(file));
    JPConfiguration other((dir / "other").string());
    std::string error;
    auto read = other.board(file, error);
    assert(read && other.parts().empty());
    const JPBoardPart* copied = read->part("bp-1");
    assert(copied && copied->copyPart && copied->fingerprint == bp.fingerprint && other.differs(*copied));
    const JPPart* placedBy = other.part(read->find("C1")->partId);
    assert(placedBy && std::abs(placedBy->height.value() - 1.0) < 1e-9 && other.package("C0603"));
    // Given to that library, it is in it (and the board no longer differs).
    other.giveCopy(*copied);
    assert(other.parts().size() == 1 && !other.differs(*copied));
    fs::remove_all(dir);
    return 0;
}
