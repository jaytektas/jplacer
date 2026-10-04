// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// Placements given their parts, strongest field first: a supplier number,
// an MPN, value with ratings and package as a guess, else a new part in a
// package found by name or made new. Placements that are the same share a
// part; the library is not changed. Each placement's state names its first
// missing link, a conflict, or a guess.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "library/JPPartMatcher.h"
#include "library/JPPlacementState.h"

using namespace jf;

namespace {

using K = JPPlacementState::Kind;

// A library with an 0603 and an 0402 package (footprints with two pads), a
// numbered 10k in 0603, and a generic 100 nF X7R in 0402.
JPPartsStore library() {
    JPPartsStore s;
    auto twoPads = [](const char* name) {
        JPFootprint f;
        f.name = name;
        f.pads = { { "1", -0.5, 0, 0.5, 0.5, 0, 0 }, { "2", 0.5, 0, 0.5, 0.5, 0, 0 } };
        f.pin1 = "1";
        return f;
    };
    JPPackage k0603;
    k0603.name = "0603";
    k0603.footprintId = s.add(twoPads("R0603"));
    const std::string p0603 = s.add(std::move(k0603));
    s.addName(p0603, "R0603");
    JPPackage k0402;
    k0402.name = "0402";
    k0402.footprintId = s.add(twoPads("C0402"));
    const std::string p0402 = s.add(std::move(k0402));
    s.addName(p0402, "C0402");
    s.addName(p0402, "0402");
    JPPart r;
    r.mpn = "0603WAF1002T5E";
    r.value = "10k";
    r.supplierNumbers = { { "LCSC", "C25804" } };
    r.packageId = p0603;
    s.add(std::move(r));
    JPPart c;
    c.value = "100nF";
    c.dielectric = "X7R";
    c.packageId = p0402;
    s.add(std::move(c));
    return s;
}

JPPlacement at(const char* designator) {
    JPPlacement p;
    p.designator = designator;
    return p;
}

} // namespace

int main() {
    const JPPartsStore lib = library();
    const size_t libParts = lib.parts.size();
    JPPartsStore job;
    std::vector<JPPlacement> ps;

    // By supplier number (written with no supplier), and a second the same.
    JPPlacement r1 = at("R1");
    r1.supplierNumbers = { { "", "C25804" } };
    r1.footprint = "R0603";
    ps.push_back(r1);
    JPPlacement r2 = r1;
    r2.designator = "R2";
    ps.push_back(r2);
    // By MPN, written another way.
    JPPlacement r3 = at("R3");
    r3.mpn = "0603waf1002t5e";
    r3.footprint = "R0603";
    ps.push_back(r3);
    // A guess: value written another way, the dielectric agreeing, the package by name.
    JPPlacement c1 = at("C1");
    c1.value = "0.1uF";
    c1.dielectric = "X7R";
    c1.footprint = "C0402";
    ps.push_back(c1);
    // Not a guess: the dielectric differs, so a new part in the library's 0402.
    JPPlacement c2 = at("C2");
    c2.value = "100nF";
    c2.dielectric = "C0G";
    c2.footprint = "C0402";
    ps.push_back(c2);
    // A new part in a package no one has: a new package by that name, no footprint.
    JPPlacement u1 = at("U1");
    u1.mpn = "VNQ7140AJTR";
    u1.footprint = "POWERSSO-16";
    u1.supplierPackage = "PowerSSO-16";
    ps.push_back(u1);
    JPPlacement u2 = at("U2");
    u2.mpn = "VNQ9080AJTR";
    u2.footprint = "POWERSSO-16";
    ps.push_back(u2);
    // The library's 10k by number, but the file says another package: a conflict.
    JPPlacement r4 = at("R4");
    r4.supplierNumbers = { { "LCSC", "C25804" } };
    r4.footprint = "C0402";
    ps.push_back(r4);
    // Nothing to go on; a fiducial; a pin count the footprint disagrees with.
    ps.push_back(at("X1"));
    JPPlacement fid = at("FID1");
    fid.fiducial = true;
    ps.push_back(fid);
    JPPlacement r5 = at("R5");
    r5.supplierNumbers = { { "LCSC", "C25804" } };
    r5.footprint = "R0603";
    r5.pins = 3;
    ps.push_back(r5);

    const JPPartMatcher::Result res = JPPartMatcher::match(ps, lib, job);
    auto find = [&ps](const char* d) -> JPPlacement& {
        for (JPPlacement& p : ps)
            if (p.designator == d) return p;
        assert(false);
        return ps.front();
    };
    auto state = [&](const char* d) { return JPPlacementState::of(find(d), job).kind; };

    assert(lib.parts.size() == libParts);   // the library is not changed
    assert(res.certain == 5 && res.guessed == 1 && res.created == 3 && res.noPart == 1);

    // R1, R2, R3, R4, R5: one part, the library's, copied in once.
    const std::string rid = find("R1").partId;
    for (const char* d : { "R2", "R3", "R4", "R5" }) assert(find(d).partId == rid);
    assert(job.part(rid)->origin.libraryId == lib.partByMpn("0603WAF1002T5E")->id);
    assert(state("R1") == K::Ready && state("R3") == K::Ready);
    assert(state("R4") == K::Conflict && state("R5") == K::Conflict);

    // C1 a guess, C2 a new part, both in one copy of the 0402.
    assert(find("C1").partGuessed && state("C1") == K::Guess);
    assert(!find("C2").partGuessed && job.part(find("C2").partId)->dielectric == "C0G");
    assert(job.part(find("C1").partId)->packageId == job.part(find("C2").partId)->packageId);
    assert(state("C2") == K::Ready);

    // U1 and U2: two parts, one new package of both names, no footprint yet.
    const JPPart* pu1 = job.part(find("U1").partId);
    const JPPart* pu2 = job.part(find("U2").partId);
    assert(pu1 && pu2 && pu1 != pu2 && pu1->packageId == pu2->packageId);
    assert(job.package(pu1->packageId)->name == "PowerSSO-16" && job.packageNamed("POWERSSO-16") == job.package(pu1->packageId));
    assert(state("U1") == K::NoFootprint);

    assert(state("X1") == K::NoPart && state("FID1") == K::Fiducial && find("FID1").partId.empty());

    // Matching again changes nothing: every placement keeps its part.
    const size_t jobParts = job.parts.size();
    const JPPartMatcher::Result again = JPPartMatcher::match(ps, lib, job);
    assert(job.parts.size() == jobParts && again.certain == 0 && again.created == 0 && find("R1").partId == rid);
    return 0;
}
