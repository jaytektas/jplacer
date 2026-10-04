// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// The rotation check: which way can settle a package (cannot matter, the
// file's pad 1, vision, the person), the file's pad 1 against where the
// package puts it (agreeing, all off by one quarter turn, mixed; both sides),
// and a package unchecked when its footprint or turn changes.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "library/JPRotationCheck.h"
#include "library/JPStarterLibrary.h"

#include <cmath>

using namespace jf;
using W = JPRotationCheck::Way;

namespace {

const JPFootprint& fp(const JPPartsStore& s, const char* name) {
    return *s.footprint(s.packageNamed(name)->footprintId);
}

JPPlacement at(const char* d, double x, double y, double rot, JPPlacement::Side side = JPPlacement::Side::Top) {
    JPPlacement p;
    p.designator = d;
    p.x = x;
    p.y = y;
    p.rotationDeg = rot;
    p.side = side;
    return p;
}

// Pad 1 given where `f`'s pad 1 lands, turned `extra` degrees more than the placement says.
void pin1(JPPlacement& p, const JPFootprint& f, double extra) {
    const double a = (p.rotationDeg + extra) * M_PI / 180;
    const double lx = p.side == JPPlacement::Side::Bottom ? -f.pin1Pad()->x : f.pin1Pad()->x, ly = f.pin1Pad()->y;
    p.hasPin1 = true;
    p.pin1X = p.x + lx * std::cos(a) - ly * std::sin(a);
    p.pin1Y = p.y + lx * std::sin(a) + ly * std::cos(a);
}

} // namespace

int main() {
    JPPartsStore s;
    JPStarterLibrary::fill(s);
    const JPFootprint& chip = fp(s, "0603");
    const JPFootprint& soic = fp(s, "SOIC-8");
    const JPFootprint& sot = fp(s, "SOT-23");
    const JPFootprint& qfn = fp(s, "QFN-16_3x3");

    assert(JPRotationCheck::symmetric(chip, 2) && !JPRotationCheck::symmetric(chip, 1));
    assert(JPRotationCheck::symmetric(soic, 2) && !JPRotationCheck::symmetric(soic, 1));
    assert(!JPRotationCheck::symmetric(sot, 2) && !JPRotationCheck::symmetric(sot, 1));
    assert(JPRotationCheck::symmetric(qfn, 1));

    JPPlacement r1 = at("R1", 10, 10, 0), c1 = at("C1", 12, 10, 90), d1 = at("D1", 14, 10, 0);
    JPPlacement u1 = at("U1", 20, 20, 90), q1 = at("Q1", 30, 20, 0);
    assert(JPRotationCheck::way(chip, { &r1, &c1 }) == W::CannotMatter);
    assert(JPRotationCheck::way(chip, { &r1, &d1 }) == W::Person);   // a diode: polarised
    assert(JPRotationCheck::way(soic, { &u1 }) == W::Person);
    assert(JPRotationCheck::way(sot, { &q1 }) == W::Vision);
    assert(JPRotationCheck::way(qfn, { &u1 }) == W::Person);
    pin1(u1, soic, 0);
    assert(JPRotationCheck::way(soic, { &u1 }) == W::File);

    // The file agrees: pad 1 where the package puts it, top and bottom.
    JPPlacement u2 = at("U2", 50, 20, 180, JPPlacement::Side::Bottom);
    pin1(u2, soic, 0);
    JPRotationCheck::FileVerdict v = JPRotationCheck::byFile(soic, 0, { &u1, &u2 });
    assert(v.compared == 2 && v.agreeing == 2 && v.uniform && v.quarters == 0 && v.odd.empty());
    // Every one off by a quarter turn: the package's turn is out by that.
    pin1(u1, soic, 90);
    pin1(u2, soic, 90);
    v = JPRotationCheck::byFile(soic, 0, { &u1, &u2 });
    assert(v.compared == 2 && v.agreeing == 0 && v.uniform && v.quarters == 1);
    // With that turn on the package, they agree.
    v = JPRotationCheck::byFile(soic, 90, { &u1, &u2 });
    assert(v.agreeing == 2 && v.quarters == 0);
    // Mixed: one off, named.
    pin1(u2, soic, 0);
    JPPlacement u3 = at("U3", 70, 20, 0);
    pin1(u3, soic, 90);
    v = JPRotationCheck::byFile(soic, 0, { &u1, &u2, &u3 });
    assert(!v.uniform && v.quarters == 1 && v.odd.size() == 1 && v.odd[0] == "U2");

    // Checked until its footprint or turn changes.
    JPPackage k = *s.packageNamed("SOIC-8");
    assert(!JPRotationCheck::checked(k));
    JPRotationCheck::markChecked(k, "file");
    assert(JPRotationCheck::checked(k));
    k.turnDeg = 180;
    assert(!JPRotationCheck::checked(k));
    JPRotationCheck::markChecked(k, "person");
    k.footprintId = "another";
    assert(!JPRotationCheck::checked(k));
    return 0;
}
