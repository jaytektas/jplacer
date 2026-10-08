// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// The job's data checked before a run (DESIGN.md, Ready to run): one list, each thing once with all it is about,
// stops first: a part not chosen, one the library lacks, no package, no nozzle tip that fits; then what to
// check (not verified, height unknown, no footprint); then notes (no feeder yet, short of stock). Placements placed, not
// enabled or on the other side are not looked at.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "model/JPBoardLocation.h"
#include "model/JPJobCheck.h"

#include <algorithm>
#include <filesystem>

using namespace jf;
namespace fs = std::filesystem;

namespace {

const JPJobCheck::Item* find(const std::vector<JPJobCheck::Item>& items, const std::string& what) {
    for (const auto& i : items)
        if (i.what == what) return &i;
    return nullptr;
}

bool has(const JPJobCheck::Item* i, const std::string& which) {
    return i && std::find(i->which.begin(), i->which.end(), which) != i->which.end();
}

} // namespace

int main() {
    const fs::path dir = fs::temp_directory_path() / "jplacer-test-job-check";
    fs::remove_all(dir);
    fs::create_directories(dir);
    JPConfiguration config(dir.string());
    std::vector<std::string> problems;
    std::string error;
    assert(config.load(problems, error));
    // R0603: fits tip NT1, has a footprint, a feeder, a height. SOT23: fits only NT9 (not on the machine), no
    // footprint, no feeder, no height. A part with no package.
    auto r0603 = std::make_shared<JPPackage>();
    r0603->id = "R0603";
    r0603->compatibleNozzleTipIds = { "NT1" };
    r0603->footprint.pads.push_back({});
    config.addPackage(r0603);
    auto sot = std::make_shared<JPPackage>();
    sot->id = "SOT23";
    sot->compatibleNozzleTipIds = { "NT9" };
    config.addPackage(sot);
    auto r = std::make_shared<JPPart>();
    r->id = "R0603-10k";
    r->packageId = "R0603";
    r->height = JPLength(0.5, JPLengthUnit::Millimeters);
    config.addPart(r);
    auto q = std::make_shared<JPPart>();
    q->id = "BC847";
    q->packageId = "SOT23";
    config.addPart(q);
    auto bare = std::make_shared<JPPart>();
    bare->id = "BARE";
    config.addPart(bare);
    JPFeeder f = JPFeeder::create(JPFeeder::classNames().front(), "R0603-10k");
    f.setEnabled(true);
    config.addFeeder(f);

    auto board = std::make_shared<JPBoard>();
    JPBoardPart unchosen;
    unchosen.key = "bp-1";
    unchosen.fields["part"] = "LM358";
    board->parts() = { unchosen };
    auto add = [&board](const std::string& id, const std::string& part, const std::string& key = "") {
        JPPlacement p;
        p.id = id;
        p.partId = part;
        p.boardPart = key;
        board->placements.push_back(p);
        return &board->placements.back();
    };
    add("R1", "R0603-10k")->verified = { "operator", "2026-10-08 12:00:00" };
    add("R2", "R0603-10k");
    add("Q1", "BC847");
    add("U1", "LM358", "bp-1");
    add("X1", "GONE");
    add("Z1", "BARE");
    add("R3", "R0603-10k")->enabled = false;
    add("R4", "R0603-10k")->side = JPSide::Bottom;
    add("R5", "R0603-10k");
    add("R1", "R0603-10k")->type = JPPlacement::Type::Fiducial;   // R1 twice
    JPJob job;
    auto l = std::make_unique<JPBoardLocation>();
    l->id = "Brd1";
    l->holder = board;
    JPPlacementsHolderLocation* where = job.addBoardOrPanelLocation(std::move(l));
    job.storePlacedStatus(*where, "R5", true);

    const auto items = JPJobCheck::of(config, job, { "NT1", "NT2" });
    const std::string at = std::string("Brd1") + JPPlacementsHolderLocation::kIdDelimiter;
    assert(JPJobCheck::stops(items) && JPJobCheck::asks(items));
    // Stops first, then checks, then notes.
    for (size_t i = 1; i < items.size(); ++i) assert(items[i - 1].level <= items[i].level);
    assert(has(find(items, "A placement ID used twice on a board"), at + "R1"));
    assert(has(find(items, "No part chosen"), at + "U1"));
    assert(has(find(items, "A part the library does not have"), at + "X1 (GONE)"));
    assert(has(find(items, "No package"), "BARE"));
    assert(has(find(items, "No nozzle tip on the machine fits its package"), "BC847 (SOT23)"));
    const auto* noFeeder = find(items, "No feeder holds it yet (the run asks for it to be loaded when it gets to it)");
    assert(has(noFeeder, "BC847") && !has(noFeeder, "R0603-10k") && noFeeder->level == JPJobCheck::Item::Level::Note);
    // Not verified: R2 and Q1; not R1 (verified), R3 (off), R4 (other side), R5 (placed).
    const auto* unverified = find(items, "Not verified on the machine");
    assert(unverified && unverified->level == JPJobCheck::Item::Level::Check && unverified->which.size() == 3
           && has(unverified, at + "R2") && has(unverified, at + "Q1") && has(unverified, at + "Z1"));
    assert(has(find(items, "Height not known"), "BC847") && !has(find(items, "Height not known"), "R0603-10k"));
    assert(has(find(items, "No footprint to draw or check against"), "BC847 (SOT23)"));
    assert(!find(items, "Short of stock (a run asks for it as it goes)"));   // no stock kept: nothing said
    // Stock kept for the 10k (a lot of 1): short by 1 of its 2 left to place (R2 and R1).
    JPStockLot lot;
    lot.partUuid = r->uuid;
    lot.label = "strip";
    JPLedgerEntry one;
    one.quantity = 1;
    assert(config.stock().addLot(lot, one, error));
    const auto again = JPJobCheck::of(config, job, { "NT1" });
    const auto* shortNote = find(again, "Short of stock (a run asks for it as it goes)");
    assert(shortNote && shortNote->level == JPJobCheck::Item::Level::Note && has(shortNote, "R0603-10k (short 1)"));

    // All put right: nothing stops, nothing asks.
    JPJob clean;
    auto good = std::make_shared<JPBoard>();
    JPPlacement p;
    p.id = "R1";
    p.partId = "R0603-10k";
    p.verified = { "operator", "2026-10-08 12:00:00" };
    good->placements.push_back(p);
    auto gl = std::make_unique<JPBoardLocation>();
    gl->id = "Brd1";
    gl->holder = good;
    clean.addBoardOrPanelLocation(std::move(gl));
    const auto none = JPJobCheck::of(config, clean, { "NT1" });
    assert(!JPJobCheck::stops(none) && !JPJobCheck::asks(none));
    fs::remove_all(dir);
    return 0;
}
