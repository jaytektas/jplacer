// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <cstddef>
#include <vector>

inline namespace jf {

// OpenPnP's 2D helpers over points (each carrying the index of what it stands for): QuickHull (the convex hull,
// in OpenPnP's order), the shoelace polygon area, Heron's triangle area, the most distant pair, all combinations of
// a size (Collect.allCombinationsOfSize), and ReferenceFiducialLocator.getBestFiducials.
class JPGeometry2D {
public:
    struct Point {
        double x = 0, y = 0;
        size_t index = 0;
    };

    // OpenPnP's QuickHull.quickHull: fewer than three points as they are.
    static std::vector<Point> quickHull(std::vector<Point> points);
    static double polygonArea(const std::vector<Point>& vertices);
    static double triangleArea(const Point& a, const Point& b, const Point& c);
    // OpenPnP's Utils2D.mostDistantPair: the two points furthest apart.
    static std::vector<Point> mostDistantPair(const std::vector<Point>& points);
    // Every combination of `size` of n items (their indices, in order); none when size > n.
    static std::vector<std::vector<size_t>> allCombinationsOfSize(size_t n, size_t size);
    // OpenPnP's getBestFiducials: fewer than three as they are; else of the hull, the three spanning the largest
    // triangle, or the two most distant when all are in a line.
    static std::vector<Point> bestFiducials(const std::vector<Point>& fiducials);
};

} // inline namespace jf
