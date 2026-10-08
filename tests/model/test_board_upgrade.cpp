// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// A board upgraded to a new revision (DESIGN.md, Board revisions): the new files' placements paired with the
// board's (by designator, then a renumbered one by footprint and position), the CAD origin's move found and
// taken back, what changed sorted, decisions carried where they hold (identity, rotation correction as a
// difference from the CAD, verified marks, board parts' choices); revisions kept in the file, switched between,
// with work done on one given to the other where the placement is the same, and that undone.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "model/JPBoardUpgrade.h"

#include <cmath>

using namespace jf;

namespace {

JPBoardPart part(const std::string& key, const std::string& footprint, const std::string& value,
                 const std::string& library = "") {
    JPBoardPart p;
    p.key = key;
    p.fields = { { "footprint", footprint }, { "value", value } };
    if (!library.empty()) {
        p.state = JPBoardPart::State::Matched;
        p.libraryPartId = library;
    }
    return p;
}

JPPlacement placement(const std::string& id, const std::string& key, double x, double y, double cad, double rotation) {
    JPPlacement p;
    p.id = id;
    p.boardPart = key;
    p.location = JPLocation(JPLengthUnit::Millimeters, x, y, 0, rotation);
    p.cadRotation = cad;
    return p;
}

bool near(double a, double b) { return std::abs(a - b) < 1e-6; }

const JPBoardUpgrade::Row* row(const JPBoardUpgrade& u, const std::string& was, const std::string& now) {
    for (const JPBoardUpgrade::Row& r : u.rows())
        if (r.was == was && r.now == now) return &r;
    return nullptr;
}

} // namespace

int main() {
    // Rev A, as it was left: matched parts, U1 turned 90° on the machine and verified, R1 verified.
    JPBoard board;
    board.file = "/work/Controller.jpboard";
    board.parts() = { part("bp-1", "R0603", "10k", "R0603-10k"), part("bp-2", "C0603", "100n", "C0603-100n"),
                      part("bp-3", "SOIC-8", "LM358", "LM358"), part("bp-4", "SOD-123", "1N4148", "1N4148"),
                      part("bp-5", "R0603", "1k", "R0603-1k") };
    board.placements = { placement("R1", "bp-1", 10, 10, 0, 0), placement("R2", "bp-1", 20, 10, 0, 0),
                         placement("C1", "bp-2", 30, 10, 90, 90), placement("U1", "bp-3", 40, 20, 0, 90),
                         placement("R12", "bp-5", 50, 30, 180, 180), placement("D1", "bp-4", 60, 30, 0, 0),
                         placement("R3", "bp-1", 70, 30, 0, 0) };
    board.find("R1")->verified = { "operator", "2026-10-08 10:00:00" };
    board.find("U1")->verified = { "operator", "2026-10-08 10:01:00" };
    board.find("C1")->verified = { "operator", "2026-10-08 10:02:00" };
    board.find("R12")->verified = { "operator", "2026-10-08 10:03:00" };
    board.find("R2")->enabled = false;
    board.syncParts();

    // Rev B's files: the origin moved 2, -1.5 mm. R2 moved half a millimetre, C1 is 220n now, U1 turned 90°
    // in the CAD, R12 is R15, D1 gone, Q1 new, R3 an 0805.
    JPBoard files;
    files.parts() = { part("f-1", "R0603", "10k"), part("f-2", "C0603", "220n"), part("f-3", "SOIC-8", "LM358"),
                      part("f-4", "R0603", "1k"), part("f-5", "SOT-23", "BC847"), part("f-6", "R0805", "10k") };
    const double dx = 2, dy = -1.5;
    files.placements = { placement("R1", "f-1", 10 + dx, 10 + dy, 0, 0), placement("R2", "f-1", 20.5 + dx, 10 + dy, 0, 0),
                         placement("C1", "f-2", 30 + dx, 10 + dy, 90, 90), placement("U1", "f-3", 40 + dx, 20 + dy, 90, 90),
                         placement("R15", "f-4", 50 + dx, 30 + dy, 180, 180), placement("Q1", "f-5", 80 + dx, 30 + dy, 0, 0),
                         placement("R3", "f-6", 70 + dx, 30 + dy, 0, 0) };
    files.syncParts();

    JPBoardUpgrade up(board, files);
    for (const JPPlacement& p : board.placements) assert(!p.uid.empty());
    assert(up.originMoved() && near(up.originX(), dx) && near(up.originY(), dy) && near(up.originTurn(), 0));
    assert(up.count(JPBoardUpgrade::Kind::Unchanged) == 1);   // R1
    assert(up.count(JPBoardUpgrade::Kind::Moved) == 2);       // R2, U1
    assert(up.count(JPBoardUpgrade::Kind::PartChanged) == 1 && row(up, "C1", "C1")->partChanged);
    assert(row(up, "C1", "C1")->detail == "part 100n → 220n");
    assert(up.count(JPBoardUpgrade::Kind::FootprintChanged) == 1 && row(up, "R3", "R3")->footprintChanged);
    assert(up.count(JPBoardUpgrade::Kind::Renamed) == 1 && row(up, "R12", "R15")->detail == "was R12");
    assert(up.count(JPBoardUpgrade::Kind::New) == 1 && row(up, "", "Q1")->toMatch);
    assert(up.count(JPBoardUpgrade::Kind::Removed) == 1 && row(up, "D1", ""));
    assert(!row(up, "U1", "U1")->toMatch && row(up, "C1", "C1")->toMatch);   // U1's line the same, its choice kept
    assert(row(up, "R2", "R2")->detail == "moved 0.50 mm" && row(up, "U1", "U1")->detail == "turned 90°");
    assert(up.summary().rfind("Origin moved 2.00, -1.50 mm; 1 unchanged, 2 moved (to verify), 1 part changed", 0) == 0);

    const std::string uidU1 = board.find("U1")->uid, uidR12 = board.find("R12")->uid;
    JPBoardRevision rev = up.revision("rev B", "2026-10-08 12:00:00");
    auto in = [&rev](const std::string& id) -> const JPPlacement* {
        for (const JPPlacement& p : rev.placements)
            if (p.id == id) return &p;
        return nullptr;
    };
    assert(rev.placements.size() == 7 && !in("D1") && in("Q1") && !in("Q1")->uid.empty());
    // Taken back to the board's origin: R1 where it was, verified still; R2 not verified, not enabled as set.
    assert(near(in("R1")->location.x(), 10) && near(in("R1")->location.y(), 10) && in("R1")->verified.by == "operator");
    assert(near(in("R2")->location.x(), 20.5) && in("R2")->verified.by.empty() && !in("R2")->enabled);
    // U1: the CAD turned it 90°; the machine's +90° correction kept, so it is placed at 180°, to be verified.
    assert(in("U1")->uid == uidU1 && near(*in("U1")->cadRotation, 90) && near(in("U1")->location.rotation(), 180));
    assert(in("U1")->verified.by.empty());
    // R15 is R12: its identity and verified mark kept. C1 (part changed) keeps its mark; R3 (footprint) not.
    assert(in("R15")->uid == uidR12 && in("R15")->verified.by == "operator");
    assert(in("C1")->verified.by == "operator" && in("R3")->verified.by.empty());
    // Board parts: the same lines keep their keys and choices; the changed ones are the import's, to choose.
    auto partOf = [&rev](const std::string& key) -> const JPBoardPart* {
        for (const JPBoardPart& p : rev.parts)
            if (p.key == key) return &p;
        return nullptr;
    };
    assert(in("R1")->boardPart == "bp-1" && partOf("bp-1")->libraryPartId == "R0603-10k");
    assert(in("U1")->boardPart == "bp-3" && in("R15")->boardPart == "bp-5");
    const JPBoardPart* c1 = partOf(in("C1")->boardPart);
    assert(c1->key != "bp-2" && c1->state == JPBoardPart::State::Unmatched && c1->field("value") == "220n");
    for (const JPBoardPart& p : rev.parts) assert(p.key != "bp-4" || p.field("value") != "1N4148");

    // Made the board's: rev A kept, rev B shown.
    board.addRevision(rev, "rev A");
    assert(board.revisions().size() == 2 && board.revision() == 1 && board.revisionLabel() == "rev B");
    assert(board.find("R15") && !board.find("D1") && board.find("U1")->partId == "LM358");
    assert(board.revisionAt(0).placements.size() == 7 && board.revisionAt(0).summary.empty());
    assert(board.revisionAt(1).summary == up.summary());

    // On rev B, R1 turned and verified, and C1's 220n matched. Back to rev A: R1 is the same there (its
    // place, CAD rotation and part), so its verified rotation is given; C1 is not (another part), so not.
    JPPlacement* r1 = board.find("R1");
    r1->location = r1->location.derive(std::nullopt, std::nullopt, std::nullopt, -90.0);
    r1->verified = { "operator", "2026-10-08 13:00:00" };
    JPBoardPart* c1b = board.part(board.find("C1")->boardPart);
    c1b->state = JPBoardPart::State::Matched;
    c1b->libraryPartId = "C0603-220n";
    board.syncParts();
    JPRevisionCarry carry = board.switchRevision(0);
    assert(board.revisionLabel() == "rev A" && board.find("D1") && board.find("R12"));
    assert(carry.from == "rev B" && carry.rotations == std::vector<std::string> { "R1" });
    assert(near(board.find("R1")->location.rotation(), -90) && board.find("R1")->verified.when == "2026-10-08 13:00:00");
    assert(board.find("C1")->partId == "C0603-100n");
    // Undone: rev A as it was.
    board.undoCarry(carry);
    assert(near(board.find("R1")->location.rotation(), 0) && board.find("R1")->verified.when == "2026-10-08 10:00:00");
    // Rev B again: as it was left there.
    board.switchRevision(1);
    assert(near(board.find("R1")->location.rotation(), -90) && board.find("C1")->partId == "C0603-220n");

    // The file keeps them all, the one shown too.
    const JPBoard back = JPBoard::fromJson(JJson::parse(board.toJson().dump()));
    assert(back.revisions().size() == 2 && back.revision() == 1 && back.revisionLabel() == "rev B");
    assert(back.find("R15")->uid == uidR12 && back.find("C1")->partId == "C0603-220n");
    const JPBoardRevision a = back.revisionAt(0);
    assert(a.label == "rev A" && a.placements.size() == 7 && a.parts.size() == board.revisionAt(0).parts.size());

    // An instance follows the revision its definition shows, keeping what a job set by identity.
    JPBoard instance = back;
    for (JPPlacement& p : instance.placements)
        if (p.id == "R15") p.enabled = false;
    JPBoard def = back;
    def.switchRevision(0);
    instance.followRevision(def);
    assert(instance.revisionLabel() == "rev A" && instance.find("R12") && !instance.find("R12")->enabled);

    // Labels counted on.
    assert(JPBoardRevision::next("rev A") == "rev B" && JPBoardRevision::next("v9") == "v10");
    assert(JPBoardRevision::next("Rev 1.2") == "Rev 1.3" && JPBoardRevision::next("first") == "first 2");
    assert(JPBoardRevision::next("") == "rev A");

    // No origin move told from two placements, and none where they did not move.
    JPBoard small;
    small.placements = { placement("R1", "", 1, 1, 0, 0), placement("R2", "", 2, 2, 0, 0) };
    JPBoard smallFiles;
    smallFiles.placements = { placement("R1", "", 5, 1, 0, 0), placement("R2", "", 6, 2, 0, 0) };
    assert(!JPBoardUpgrade(small, smallFiles).originMoved());
    return 0;
}
