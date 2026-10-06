// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPGeometry2D.h"

#include <algorithm>
#include <cmath>
#include <limits>

inline namespace jf {

namespace {

using Point = JPGeometry2D::Point;

// Which side of AB the point is on: 1 left, -1 right (or on it).
int pointLocation(const Point& a, const Point& b, const Point& p) {
    const double cp = (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
    return cp > 0 ? 1 : -1;
}

// The distance of C from the line AB, scaled (as OpenPnP's: only compared).
double distance(const Point& a, const Point& b, const Point& c) {
    return std::fabs((b.x - a.x) * (a.y - c.y) - (b.y - a.y) * (a.x - c.x));
}

void hullSet(const Point& a, const Point& b, std::vector<Point> set, std::vector<Point>& hull) {
    size_t insert = hull.size();
    for (size_t i = 0; i < hull.size(); ++i)
        if (hull[i].index == b.index) {
            insert = i;
            break;
        }
    if (set.empty()) return;
    if (set.size() == 1) {
        hull.insert(hull.begin() + long(insert), set[0]);
        return;
    }
    double best = -std::numeric_limits<double>::infinity();
    size_t furthest = 0;
    for (size_t i = 0; i < set.size(); ++i)
        if (const double d = distance(a, b, set[i]); d > best) {
            best = d;
            furthest = i;
        }
    const Point p = set[furthest];
    set.erase(set.begin() + long(furthest));
    hull.insert(hull.begin() + long(insert), p);
    std::vector<Point> leftAP, leftPB;
    for (const Point& m : set)
        if (pointLocation(a, p, m) == 1) leftAP.push_back(m);
    for (const Point& m : set)
        if (pointLocation(p, b, m) == 1) leftPB.push_back(m);
    hullSet(a, p, leftAP, hull);
    hullSet(p, b, leftPB, hull);
}

} // namespace

std::vector<Point> JPGeometry2D::quickHull(std::vector<Point> points) {
    if (points.size() < 3) return points;
    // The extremes in X.
    size_t minI = 0, maxI = 0;
    double minX = std::numeric_limits<double>::infinity(), maxX = -std::numeric_limits<double>::infinity();
    for (size_t i = 0; i < points.size(); ++i) {
        if (points[i].x < minX) {
            minX = points[i].x;
            minI = i;
        }
        if (points[i].x > maxX) {
            maxX = points[i].x;
            maxI = i;
        }
    }
    const Point a = points[minI], b = points[maxI];
    std::vector<Point> hull { a, b };
    std::erase_if(points, [&](const Point& p) { return p.index == a.index || p.index == b.index; });
    std::vector<Point> left, right;
    for (const Point& p : points) (pointLocation(a, b, p) == -1 ? left : right).push_back(p);
    hullSet(a, b, right, hull);
    hullSet(b, a, left, hull);
    return hull;
}

double JPGeometry2D::polygonArea(const std::vector<Point>& v) {
    if (v.size() < 3) return 0;
    // The shoelace formula.
    double area = 0;
    Point j = v.back();
    for (const Point& i : v) {
        area += (j.x + i.x) * (j.y - i.y);
        j = i;
    }
    return std::fabs(0.5 * area);
}

double JPGeometry2D::triangleArea(const Point& p1, const Point& p2, const Point& p3) {
    // Heron's formula, as OpenPnP's.
    const double a = std::hypot(p1.x - p2.x, p1.y - p2.y), b = std::hypot(p2.x - p3.x, p2.y - p3.y), c = std::hypot(p3.x - p1.x, p3.y - p1.y);
    const double s = (a + b + c) / 2.;
    return std::sqrt(std::max(0.0, s * (s - a) * (s - b) * (s - c)));
}

std::vector<Point> JPGeometry2D::mostDistantPair(const std::vector<Point>& points) {
    const Point* maxA = nullptr;
    const Point* maxB = nullptr;
    double max = 0;
    for (const Point& a : points)
        for (const Point& b : points) {
            if (&a == &b) continue;
            if (const double d = std::hypot(a.x - b.x, a.y - b.y); d > max) {
                maxA = &a;
                maxB = &b;
                max = d;
            }
        }
    if (!maxA) return {};
    return { *maxA, *maxB };
}

std::vector<std::vector<size_t>> JPGeometry2D::allCombinationsOfSize(size_t n, size_t size) {
    std::vector<std::vector<size_t>> out;
    if (size > n) return out;
    std::vector<size_t> idx(size);
    for (size_t i = 0; i < size; ++i) idx[i] = i;
    for (;;) {
        out.push_back(idx);
        long k = long(size) - 1;
        while (k >= 0 && idx[size_t(k)] == n - size + size_t(k)) --k;
        if (k < 0) break;
        ++idx[size_t(k)];
        for (size_t i = size_t(k) + 1; i < size; ++i) idx[i] = idx[i - 1] + 1;
    }
    return out;
}

std::vector<Point> JPGeometry2D::bestFiducials(const std::vector<Point>& fiducials) {
    if (fiducials.size() < 3) return fiducials;
    // The convex hull: the outer bounds, cutting down the triangles to try; each fiducial once (as OpenPnP's set:
    // with all sharing X, the hull holds one twice).
    std::vector<Point> hull;
    for (const Point& p : quickHull(fiducials))
        if (std::none_of(hull.begin(), hull.end(), [&p](const Point& q) { return q.index == p.index; })) hull.push_back(p);
    // The largest triangle of three of them.
    std::vector<Point> best;
    double bestArea = 0;
    for (const auto& c : allCombinationsOfSize(hull.size(), 3)) {
        const double a = triangleArea(hull[c[0]], hull[c[1]], hull[c[2]]);
        if (best.empty() || a > bestArea) {
            best = { hull[c[0]], hull[c[1]], hull[c[2]] };
            bestArea = a;
        }
    }
    // All degenerate (in a line): the two most distant.
    if (bestArea == 0) return mostDistantPair(fiducials);
    return best;
}

} // inline namespace jf
