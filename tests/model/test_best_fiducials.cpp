// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// OpenPnP's ReferenceFiducialLocatorTest: getBestFiducials keeps one or two as they are; of many, the three
// spanning the largest triangle; of three in a line (diagonal, sharing X, sharing Y), the two most distant.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>
#include <random>
#include <vector>

#include "model/JPGeometry2D.h"

using namespace jf;
using Point = JPGeometry2D::Point;

namespace {

std::vector<Point> points(std::initializer_list<std::pair<double, double>> xy) {
    std::vector<Point> out;
    for (const auto& [x, y] : xy) out.push_back({ x, y, out.size() });
    return out;
}

} // namespace

int main() {
    // testJust1, testJust2
    assert(JPGeometry2D::bestFiducials(points({ { 0, 0 } })).size() == 1);
    assert(JPGeometry2D::bestFiducials(points({ { 0, 0 }, { 0, 50 } })).size() == 2);
    // testNominal: seven placed, a hundred random inside them.
    {
        std::vector<Point> p = points({ { 0, 0 }, { 0, 50 }, { 10, 40 }, { 80, 10 }, { 100, 50 }, { 100, 0 }, { 110, 40 } });
        std::mt19937 rng(7);
        std::uniform_real_distribution<double> ux(0, 109), uy(0, 49);
        for (int i = 0; i < 100; i++) p.push_back({ ux(rng), uy(rng), p.size() });
        assert(JPGeometry2D::bestFiducials(p).size() == 3);
    }
    // testCollinear, testSameX, testSameY
    assert(JPGeometry2D::bestFiducials(points({ { 0, 0 }, { 100, 100 }, { 200, 200 } })).size() == 2);
    assert(JPGeometry2D::bestFiducials(points({ { 10, 0 }, { 10, 100 }, { 10, 200 } })).size() == 2);
    assert(JPGeometry2D::bestFiducials(points({ { 0, 10 }, { 100, 10 }, { 200, 10 } })).size() == 2);
    // And which: the line's ends.
    const auto ends = JPGeometry2D::bestFiducials(points({ { 10, 0 }, { 10, 100 }, { 10, 200 } }));
    assert((ends[0].index == 0 && ends[1].index == 2) || (ends[0].index == 2 && ends[1].index == 0));
    return 0;
}
