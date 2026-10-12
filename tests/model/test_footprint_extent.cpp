// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// A footprint's size (what Hold Footprint Size keeps as big on a camera's screen): the larger side of the box
// round its pads and its body, in millimetres, whatever units it is kept in, a turned pad as it lies; nothing to
// it: 0.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "model/JPFootprint.h"

#include <cmath>

using namespace jf;

namespace {

bool near(double a, double b) { return std::abs(a - b) < 1e-6; }

} // namespace

int main() {
    // An 0603: two 0.9 x 0.95 pads 1.6 apart (centres): 2.5 wide across its pads, 0.95 tall.
    JPFootprint c0603;
    c0603.pads.push_back({ "1", -0.8, 0, 0.9, 0.95, 0, true, 0 });
    c0603.pads.push_back({ "2", 0.8, 0, 0.9, 0.95, 0, false, 0 });
    assert(near(c0603.extentMm(), 2.5));
    // Its body bigger than its pads: the body's.
    c0603.bodyWidth = 3.2;
    c0603.bodyHeight = 1.6;
    assert(near(c0603.extentMm(), 3.2));

    // Kept in inches: given in millimetres (a 0.1 in square pad: 2.54 mm).
    JPFootprint inches;
    inches.units = JPLengthUnit::Inches;
    inches.pads.push_back({ "1", 0, 0, 0.1, 0.1, 0, false, 0 });
    assert(near(inches.extentMm(), 2.54));

    // A pad turned 90°: as it lies (1 x 3 standing, 3 tall).
    JPFootprint turned;
    turned.pads.push_back({ "1", 0, 0, 3, 1, 90, false, 0 });
    assert(std::abs(turned.extentMm() - 3) < 1e-3);

    // Nothing to it.
    assert(JPFootprint().extentMm() == 0);
    return 0;
}
