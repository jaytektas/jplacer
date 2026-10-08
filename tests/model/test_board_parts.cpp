// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// A board's own parts list (DESIGN.md, Board part): an import's parts taken into a board (a choice already
// made there kept; the board's own parts' ids scoped to it), one placement's part chosen by hand (split from
// the others when they share it, what the file said kept), parts no placement names dropped, and the board
// file of jplacer's carrying all of it, the board's own part and package too.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "model/JPBoard.h"

#include <filesystem>

using namespace jf;

namespace {

JPPlacement placement(const std::string& id, const std::string& key) {
    JPPlacement p;
    p.id = id;
    p.boardPart = key;
    return p;
}

// An import's board: C1 and C2 a 100n the board makes its own, R1 a 10k the library has, U1 unmatched.
JPBoard imported() {
    JPBoard b;
    JPBoardPart own;
    own.key = "bp-1";
    own.fields = { { "part", "C0603-100n" }, { "value", "100n" }, { "footprint", "C0603" } };
    own.state = JPBoardPart::State::Local;
    own.localPart = std::make_shared<JPPart>();
    own.localPart->id = "C0603-100n";
    own.localPart->packageId = "C0603";
    own.localPackage = std::make_shared<JPPackage>();
    own.localPackage->id = "C0603";
    JPBoardPart lib;
    lib.key = "bp-2";
    lib.fields = { { "part", "R0603-10k" } };
    lib.state = JPBoardPart::State::Matched;
    lib.libraryPartId = "R0603-10k";
    JPBoardPart none;
    none.key = "bp-3";
    none.fields = { { "part", "SOIC8-LM358" }, { "value", "LM358" } };
    b.parts() = { own, lib, none };
    b.placements = { placement("C1", "bp-1"), placement("C2", "bp-1"), placement("R1", "bp-2"), placement("U1", "bp-3") };
    b.syncParts();
    return b;
}

} // namespace

int main() {
    // Taken into a board: the board's own part's ids scoped to the board, copies of their own.
    JPBoard board;
    board.file = "/work/Controller.jpboard";
    JPBoard read = imported();
    auto keys = board.takeParts(read);
    assert(board.parts().size() == 3 && keys.size() == 3);
    const JPBoardPart* c = board.part(keys["bp-1"]);
    assert(c->state == JPBoardPart::State::Local && c->localPart->id == "Controller/C0603-100n");
    assert(c->localPackage->id == "Controller/C0603" && c->localPart->packageId == "Controller/C0603");
    assert(c->localPart != read.part("bp-1")->localPart && read.part("bp-1")->localPart->id == "C0603-100n");
    assert(board.part(keys["bp-2"])->partId() == "R0603-10k" && board.part(keys["bp-3"])->partId() == "SOIC8-LM358");
    for (const JPPlacement& p : read.placements) board.placements.push_back(placement(p.id, keys[p.boardPart]));
    board.syncParts();
    assert(board.find("C2")->partId == "Controller/C0603-100n" && board.find("U1")->partId == "SOIC8-LM358");

    // U1 matched by hand to a library part: it alone used its board part, so that one is matched (the
    // file's fields kept).
    const std::string u1 = board.matchPlacement("U1", "LM358N");
    assert(u1 == board.find("U1")->boardPart && board.part(u1)->state == JPBoardPart::State::Matched);
    assert(board.part(u1)->field("value") == "LM358");
    // C1 chosen another part: C2 keeps the board's own; C1 gets a part of its own, the same fields.
    const std::string c1 = board.matchPlacement("C1", "GRM188-100n");
    assert(c1 != board.find("C2")->boardPart && board.part(c1)->field("value") == "100n");
    board.find("C1")->boardPart = c1;
    board.syncParts();
    assert(board.find("C1")->partId == "GRM188-100n" && board.find("C2")->partId == "Controller/C0603-100n");

    // Imported again: the choice made here (U1's match) is not undone; what the file says now is kept.
    read.part("bp-3")->fields["value"] = "LM358 (TI)";
    keys = board.takeParts(read);
    assert(keys["bp-3"] == u1 && board.part(u1)->state == JPBoardPart::State::Matched
           && board.part(u1)->field("value") == "LM358 (TI)");

    // A part no placement names is dropped.
    board.placements.erase(board.placements.begin() + 2);   // R1
    board.dropUnusedParts();
    for (const JPBoardPart& p : board.parts()) assert(p.field("part") != "R0603-10k");

    // The file: every part, its fields, its own part and package, read back as they were.
    const JPBoard back = JPBoard::fromJson(JJson::parse(board.toJson().dump()));
    assert(back.parts().size() == board.parts().size() && back.placements.size() == board.placements.size());
    const JPBoardPart* c2 = back.part(back.find("C2")->boardPart);
    assert(c2->state == JPBoardPart::State::Local && c2->localPart->id == "Controller/C0603-100n"
           && c2->localPackage && c2->localPackage->id == "Controller/C0603" && c2->field("footprint") == "C0603");
    assert(back.find("C2")->partId == "Controller/C0603-100n" && back.find("U1")->partId == "LM358N");
    assert(JPBoard::isJplacerFile("a/b.jpboard") && !JPBoard::isJplacerFile("a/b.board.xml"));
    return 0;
}
