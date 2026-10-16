// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// A board scan's footprint outlines (JPBoardScan::padsOf, sizeOf): an 0603's two pads, as its footprint has them;
// turned a quarter turn, along Y; seen from the bottom side, mirrored before turning; pad 1's mark not drawn (not
// copper); a footprint given in inches, in millimetres. The box round them, its larger side.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "tasks/JPBoardScan.h"

#include <algorithm>
#include <cmath>

using namespace jf;

namespace {

bool near(double a, double b) { return std::abs(a - b) < 1e-9; }

// The middle of an outline's corners.
std::array<double, 2> middle(const std::vector<std::array<double, 2>>& o) {
    double x = 0, y = 0;
    for (const auto& p : o) {
        x += p[0];
        y += p[1];
    }
    return { x / double(o.size()), y / double(o.size()) };
}

} // namespace

int main() {
    JPFootprint f;
    f.pads.push_back({ "1", -0.75, 0, 0.8, 0.9, 0, true });
    f.pads.push_back({ "2", 0.75, 0, 0.8, 0.9, 0, false });
    auto pads = JPBoardScan::padsOf(f, 0, false);
    assert(pads.size() == 2);   // pad 1's mark not among them
    auto a = middle(pads[0]), b = middle(pads[1]);
    assert(near(a[0], -0.75) && near(a[1], 0) && near(b[0], 0.75));
    assert(near(JPBoardScan::sizeOf(pads), 2.3));
    // A quarter turn: along Y, pad 1 below.
    pads = JPBoardScan::padsOf(f, 90, false);
    a = middle(pads[0]);
    assert(std::abs(a[0]) < 1e-9 && near(a[1], -0.75));
    // From the bottom side: mirrored, then turned.
    pads = JPBoardScan::padsOf(f, 90, true);
    a = middle(pads[0]);
    assert(std::abs(a[0]) < 1e-9 && near(a[1], 0.75));
    // In inches: millimetres.
    JPFootprint in;
    in.units = JPLengthUnit::Inches;
    in.pads.push_back({ "1", 0, 0, 0.1, 0.1, 0, false });
    assert(near(JPBoardScan::sizeOf(JPBoardScan::padsOf(in, 0, false)), 2.54));
    assert(JPBoardScan::padsOf(JPFootprint {}, 0, false).empty() && JPBoardScan::sizeOf({}) == 0);
    return 0;
}
