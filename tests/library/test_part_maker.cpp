// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// A new part made from a placement: its fields copied, in the package its
// names find (the job's, else the library's copied in, else a new one of
// that name with no footprint). The library is not changed.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "library/JPPartMaker.h"
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
    const size_t libParts = lib.parts.size(), libPackages = lib.packages.size();
    JPPartsStore job;
    auto state = [&job](const JPPlacement& p) { return JPPlacementState::of(p, job).kind; };

    // In the library's 0402, copied into the job; its fields copied onto the part.
    JPPlacement c2 = at("C2");
    c2.value = "100nF";
    c2.dielectric = "C0G";
    c2.footprint = "C0402";
    c2.partId = JPPartMaker::fromPlacement(c2, lib, job);
    const JPPart* pc2 = job.part(c2.partId);
    assert(pc2 && pc2->value == "100nF" && pc2->dielectric == "C0G");
    assert(job.package(pc2->packageId)->origin.libraryId == lib.packageNamed("C0402")->id);
    assert(state(c2) == K::Ready);

    // A package no one has: a new one of both names, no footprint; the next finds it.
    JPPlacement u1 = at("U1");
    u1.mpn = "VNQ7140AJTR";
    u1.footprint = "POWERSSO-16";
    u1.supplierPackage = "PowerSSO-16";
    u1.partId = JPPartMaker::fromPlacement(u1, lib, job);
    JPPlacement u2 = at("U2");
    u2.mpn = "VNQ9080AJTR";
    u2.footprint = "POWERSSO-16";
    u2.partId = JPPartMaker::fromPlacement(u2, lib, job);
    const JPPart* pu1 = job.part(u1.partId);
    const JPPart* pu2 = job.part(u2.partId);
    assert(pu1 && pu2 && pu1 != pu2 && pu1->packageId == pu2->packageId && pu1->mpn == "VNQ7140AJTR");
    assert(job.package(pu1->packageId)->name == "PowerSSO-16" && job.packageNamed("POWERSSO-16") == job.package(pu1->packageId));
    assert(state(u1) == K::NoFootprint);

    // No names at all: a part with no package.
    JPPlacement x1 = at("X1");
    x1.value = "?";
    x1.partId = JPPartMaker::fromPlacement(x1, lib, job);
    assert(job.part(x1.partId)->packageId.empty() && state(x1) == K::NoPackage);

    assert(lib.parts.size() == libParts && lib.packages.size() == libPackages);   // the library is not changed
    return 0;
}
