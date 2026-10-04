// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// A board read again: what it adds, removes, moves, turns or makes a
// different part, and what the person set on the old one, kept where it
// still holds.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "job/JPBoardChanges.h"

using namespace jf;
using K = JPBoardChanges::Change::Kind;

namespace {

JPPlacement at(const char* d, double x, double y, double rot, const char* value) {
    JPPlacement p;
    p.designator = d;
    p.x = x;
    p.y = y;
    p.rotationDeg = rot;
    p.value = value;
    p.footprint = "C0402";
    return p;
}

int count(const std::vector<JPBoardChanges::Change>& cs, K k) {
    int n = 0;
    for (const auto& c : cs) n += c.kind == k ? 1 : 0;
    return n;
}

} // namespace

int main() {
    JPBoard before;
    before.placements = { at("C1", 1, 1, 0, "100nF"), at("C2", 2, 2, 0, "100nF"), at("C3", 3, 3, 0, "100nF"),
                          at("C4", 4, 4, 0, "100nF"), at("C77", 7, 7, 0, "1uF") };
    for (JPPlacement& p : before.placements) p.partId = "part-" + p.designator;
    before.placements[1].rotationSet = true;   // C2 turned by hand
    before.placements[1].rotationSetDeg = 45;

    JPBoard after;
    after.placements = { at("C1", 1, 1, 0, "100nF"),          // unchanged
                         at("C2", 2.4, 2, 0, "100nF"),        // moved 0.4
                         at("C3", 3, 3, 90, "100nF"),         // turned
                         at("C4", 4, 4, 0, "1uF"),            // a different part
                         at("R140", 9, 9, 0, "10k") };        // new; C77 removed

    const auto cs = JPBoardChanges::compare(before, after);
    assert(count(cs, K::Added) == 1 && count(cs, K::Removed) == 1 && count(cs, K::Moved) == 1);
    assert(count(cs, K::Turned) == 1 && count(cs, K::DifferentPart) == 1 && cs.size() == 5);
    for (const auto& c : cs)
        if (c.kind == K::Moved) assert(c.designator == "C2" && c.text == "moved 0.40 mm");

    const JPBoard merged = JPBoardChanges::merge(before, after);
    assert(merged.find("C1")->partId == "part-C1");
    assert(merged.find("C2")->partId == "part-C2" && merged.find("C2")->rotationSet && merged.find("C2")->rotationSetDeg == 45);
    assert(merged.find("C3")->partId == "part-C3");
    assert(merged.find("C4")->partId.empty());          // a different part: to be found again
    assert(merged.find("R140")->partId.empty() && !merged.find("C77"));
    return 0;
}
