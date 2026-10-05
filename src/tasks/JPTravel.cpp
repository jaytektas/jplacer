// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPTravel.h"

#include <algorithm>
#include <cmath>
#include <limits>

inline namespace jf {

namespace {

// The most 2-opt passes made.
constexpr int kMostPasses = 50;

double distance(const JPLocation& a, const JPLocation& b) {
    const JPLocation x = a.convertToUnits(JPLengthUnit::Millimeters), y = b.convertToUnits(JPLengthUnit::Millimeters);
    return std::hypot(x.x() - y.x(), x.y() - y.y());
}

} // namespace

std::vector<size_t> JPTravel::order(const std::vector<JPLocation>& points, const std::optional<JPLocation>& start,
                                    const std::optional<JPLocation>& end) {
    const size_t n = points.size();
    std::vector<size_t> order;
    if (n == 0) return order;
    std::vector<bool> used(n, false);
    std::optional<JPLocation> at = start;
    for (size_t k = 0; k < n; ++k) {
        size_t best = n;
        double bestD = std::numeric_limits<double>::max();
        for (size_t i = 0; i < n; ++i) {
            if (used[i]) continue;
            const double d = at ? distance(*at, points[i]) : 0;
            if (d < bestD) {
                bestD = d;
                best = i;
            }
        }
        used[best] = true;
        order.push_back(best);
        at = points[best];
    }
    // The path's length, from the start and to the end where given.
    auto length = [&](const std::vector<size_t>& o) {
        double l = start ? distance(*start, points[o.front()]) : 0;
        for (size_t i = 1; i < o.size(); ++i) l += distance(points[o[i - 1]], points[o[i]]);
        if (end) l += distance(points[o.back()], *end);
        return l;
    };
    bool better = true;
    for (int pass = 0; better && pass < kMostPasses; ++pass) {
        better = false;
        for (size_t i = 0; i + 1 < n; ++i)
            for (size_t j = i + 1; j < n; ++j) {
                std::vector<size_t> trial = order;
                std::reverse(trial.begin() + long(i), trial.begin() + long(j) + 1);
                if (length(trial) + 1e-9 < length(order)) {
                    order = trial;
                    better = true;
                }
            }
    }
    return order;
}

} // inline namespace jf
