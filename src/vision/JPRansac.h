// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <vector>

inline namespace jf {

// OpenPnP's Ransac: lines through points, found by trying random pairs and
// keeping the points near each line, here only those spaced along it as
// pitched holes are (an even spacing, give or take, with none missing when
// `uninterrupted`). Longest-supported first, each line the farthest pair of
// its points. The pairs are drawn from a fixed seed, so the same points give
// the same lines.
class JPRansac {
public:
    struct Point {
        double x = 0, y = 0;
    };
    struct Line {
        Point a, b;
    };

    static std::vector<Line> lines(const std::vector<Point>& points, int maxIterations, double pointToLineDistance,
                                   double pointSpacing, double pointSpacingEpsilon, bool uninterrupted);
    // FluentCv.pointToLineDistance: P from the line through A and B.
    static double pointToLineDistance(const Point& a, const Point& b, const Point& p);
};

} // inline namespace jf
