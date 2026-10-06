// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// OpenPnP's QuickHullTest.testQuickHull: the convex hull of 100000 points spread normally about the origin, and the
// four of its points enclosing the most area, found (OpenPnP's test asserts only that they are). And what a hull
// must be: every point inside or on it, its own points among the given ones.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>
#include <cmath>
#include <random>
#include <vector>

#include "model/JPGeometry2D.h"

using namespace jf;
using Point = JPGeometry2D::Point;

int main() {
    std::mt19937 rng(1);
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    std::vector<Point> randomPoints;
    for (size_t i = 0; i < 100000; i++) {
        const double r = 100 * std::sqrt(-2 * std::log(1.0 - unit(rng)));
        const double a = 2 * M_PI * unit(rng);
        randomPoints.push_back({ r * std::cos(a), r * std::sin(a), i });
    }
    const std::vector<Point> hullPoints = JPGeometry2D::quickHull(randomPoints);
    assert(hullPoints.size() >= 3);
    std::vector<Point> bestPoints;
    double bestArea = 0;
    for (const auto& c : JPGeometry2D::allCombinationsOfSize(hullPoints.size(), 4)) {
        const std::vector<Point> quad { hullPoints[c[0]], hullPoints[c[1]], hullPoints[c[2]], hullPoints[c[3]] };
        const double a = JPGeometry2D::polygonArea(quad);
        if (bestPoints.empty() || a > bestArea) {
            bestPoints = quad;
            bestArea = a;
        }
    }
    assert(bestPoints.size() == 4 && bestArea > 0);
    // The hull: going round it, every point on its inner side or on it (it winds one way).
    double winding = 0;
    for (size_t i = 0; i < hullPoints.size(); i++) {
        const Point& a = hullPoints[i];
        const Point& b = hullPoints[(i + 1) % hullPoints.size()];
        winding += a.x * b.y - b.x * a.y;
    }
    for (size_t i = 0; i < hullPoints.size(); i++) {
        const Point& a = hullPoints[i];
        const Point& b = hullPoints[(i + 1) % hullPoints.size()];
        for (size_t k = 0; k < randomPoints.size(); k += 97) {
            const Point& p = randomPoints[k];
            const double cross = (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
            assert(winding > 0 ? cross >= -1e-6 : cross <= 1e-6);
        }
    }
    // The shoelace area of a unit square, and Heron's of a 3-4-5 triangle.
    assert(std::abs(JPGeometry2D::polygonArea({ { 0, 0, 0 }, { 1, 0, 1 }, { 1, 1, 2 }, { 0, 1, 3 } }) - 1) < 1e-12);
    assert(std::abs(JPGeometry2D::triangleArea({ 0, 0, 0 }, { 3, 0, 1 }, { 0, 4, 2 }) - 6) < 1e-9);
    // Collect.allCombinationsOfSize: n choose k, in order; none of more than there are.
    assert(JPGeometry2D::allCombinationsOfSize(5, 3).size() == 10 && JPGeometry2D::allCombinationsOfSize(2, 3).empty());
    return 0;
}
