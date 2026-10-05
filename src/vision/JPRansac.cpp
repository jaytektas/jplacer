// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPRansac.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <random>
#include <set>

inline namespace jf {

namespace {

// The pairs drawn from this seed (OpenPnP shuffles at random).
constexpr unsigned kSeed = 4481;

using P = JPRansac::Point;

double dot(const P& a, const P& b) { return a.x * b.x + a.y * b.y; }

// OpenPnP's filterInliersWithSpacing: the inliers at a whole number of
// spacings from the first point (one each), none missing between when uninterrupted, else two adjacent at least.
std::vector<int> spaced(const P& first, const P& second, const std::vector<P>& points, const std::vector<int>& inliers, double spacing,
                        double epsilon, bool uninterrupted) {
    const P dir { second.x - first.x, second.y - first.y };
    std::set<int> onLine;
    std::vector<int> out;
    for (const int i : inliers) {
        const P diff { points[size_t(i)].x - first.x, points[size_t(i)].y - first.y };
        const double distance = std::sqrt(dot(diff, diff));
        const double variance = std::fmod(distance, spacing);
        if (variance <= epsilon || spacing - variance <= epsilon) {
            const double signedDistance = distance * (dot(dir, diff) > 0.0 ? 1.0 : -1.0);
            const int index = int(std::floor(signedDistance / spacing + 0.5));
            if (onLine.insert(index).second) out.push_back(i);
        }
    }
    if (onLine.empty()) return out;
    const int lo = *onLine.begin(), hi = *onLine.rbegin();
    if (uninterrupted) {
        for (int k = lo + 1; k < hi; ++k)
            if (!onLine.count(k)) return {};
    } else {
        bool adjacent = false;
        for (int k = lo; k < hi && !adjacent; ++k) adjacent = onLine.count(k) && onLine.count(k + 1);
        if (!adjacent) return {};
    }
    return out;
}

// The line through the farthest pair of a line's points.
JPRansac::Line longest(const std::vector<P>& points, const std::vector<int>& indices) {
    int ba = 0, bb = 0;
    double best = 0;
    for (size_t i = 0; i + 1 < indices.size(); ++i)
        for (size_t j = i + 1; j < indices.size(); ++j) {
            const P& a = points[size_t(indices[i])];
            const P& b = points[size_t(indices[j])];
            const double d = std::hypot(b.x - a.x, b.y - a.y);
            if (d > best) {
                best = d;
                ba = indices[i];
                bb = indices[j];
            }
        }
    return { points[size_t(ba)], points[size_t(bb)] };
}

} // namespace

double JPRansac::pointToLineDistance(const Point& a, const Point& b, const Point& p) {
    const double normal = std::hypot(b.x - a.x, b.y - a.y);
    return std::abs((p.x - a.x) * (b.y - a.y) - (p.y - a.y) * (b.x - a.x)) / normal;
}

std::vector<JPRansac::Line> JPRansac::lines(const std::vector<Point>& points, int maxIterations, double pointToLine,
                                            double pointSpacing, double pointSpacingEpsilon, bool uninterrupted) {
    if (points.size() < 2) return {};
    std::vector<int> mapping(points.size());
    std::iota(mapping.begin(), mapping.end(), 0);
    std::mt19937 rng(kSeed);
    std::vector<std::vector<int>> found;
    for (int it = 0; it < maxIterations; ++it) {
        std::shuffle(mapping.begin(), mapping.end(), rng);
        const Point& a = points[size_t(mapping[0])];
        const Point& b = points[size_t(mapping[1])];
        std::vector<int> inliers;
        for (const int i : mapping)
            if (pointToLineDistance(a, b, points[size_t(i)]) <= pointToLine) inliers.push_back(i);
        std::vector<int> kept = spaced(a, b, points, inliers, pointSpacing, pointSpacingEpsilon, uninterrupted);
        if (kept.size() < 2) continue;
        std::sort(kept.begin(), kept.end());
        if (std::find(found.begin(), found.end(), kept) == found.end()) found.push_back(std::move(kept));
    }
    // Most points first (as found, between equals).
    std::stable_sort(found.begin(), found.end(), [](const auto& x, const auto& y) { return x.size() > y.size(); });
    std::vector<Line> out;
    for (const auto& f : found) out.push_back(longest(points, f));
    return out;
}

} // inline namespace jf
