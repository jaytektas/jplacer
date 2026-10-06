// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// A port of OpenPnP's TravellingSalesman and TravelCost, kept to their structure (and their random numbers, through
// JPJavaRandom) so they give the same routes.

#include "JPTravel.h"

#include "common/JPJavaRandom.h"

#include <algorithm>
#include <cmath>

inline namespace jf {

namespace {

// Before the current route's cost becomes the global best: scaled, so rounding does not copy routes needlessly.
constexpr double kGlobalBestDistanceScalingFactor = 1.0 - 1e-5;

JPLocation mm(const JPLocation& l) { return l.convertToUnits(JPLengthUnit::Millimeters); }

// OpenPnP's TravelCost.estimateCost: the time to travel `distance` from and to standstill.
double estimateCost(double distance, const JPTravel::Axis& axis) {
    distance = std::abs(distance);
    const double shortDistanceLimit = axis.feedrate * axis.feedrate / axis.acceleration;
    // A short way: only the acceleration counts.
    if (distance < shortDistanceLimit) return 2 * std::sqrt(distance / axis.acceleration);
    return axis.feedrate / axis.acceleration + distance / axis.feedrate;
}

} // namespace

double JPTravel::Cost::xyzCost(const JPLocation& a, const JPLocation& b) const {
    double cost = std::max(estimateCost(a.x() - b.x(), x), estimateCost(a.y() - b.y(), y));
    if (z) cost = std::max(cost, estimateCost(a.z() - b.z(), *z));
    return cost;
}

JPTravel::JPTravel(std::vector<JPLocation> points, std::optional<JPLocation> start, std::optional<JPLocation> end,
                   std::optional<Cost> cost)
    : m_cost(std::move(cost)) {
    for (const JPLocation& p : points) m_points.push_back(mm(p));
    if (start) m_start = mm(*start);
    if (end) m_end = mm(*end);
    for (size_t i = 0; i < m_points.size(); ++i) m_travel.push_back(i);
}

const JPLocation* JPTravel::location(int i) const {
    if (i < 0) return m_start ? &*m_start : nullptr;
    if (i >= int(m_travel.size())) return m_end ? &*m_end : nullptr;
    return &m_points[m_travel[size_t(i)]];
}

double JPTravel::distance(int a, int b) const {
    const JPLocation* la = location(a);
    const JPLocation* lb = location(b);
    // No start and/or end: nothing to it.
    if (!la || !lb) return 0.0;
    if (m_cost) return m_cost->xyzCost(*la, *lb);
    return std::sqrt(std::pow(la->x() - lb->x(), 2) + std::pow(la->y() - lb->y(), 2) + std::pow(la->z() - lb->z(), 2));
}

double JPTravel::travellingDistance() const {
    double d = 0.0;
    for (int i = 0; i <= int(m_travel.size()); i++) d += distance(i - 1, i);
    return d;
}

double JPTravel::swapDistance(int a, int b, bool twist) const {
    if (a > b) std::swap(a, b);   // a before b
    if (twist) {
        // The loop between them twisted round.
        const double oldSegment = distance(a - 1, a) + distance(b, b + 1);
        const double newSegment = distance(a - 1, b) + distance(a, b + 1);
        return newSegment - oldSegment;
    }
    if (a + 1 == b) {
        // Consecutive.
        const double oldSegment = distance(a - 1, a) + distance(a, b) + distance(b, b + 1);
        const double newSegment = distance(a - 1, b) + distance(b, a) + distance(a, b + 1);
        return newSegment - oldSegment;
    }
    // Apart.
    const double oldSegment = distance(a - 1, a) + distance(a, a + 1) + distance(b - 1, b) + distance(b, b + 1);
    const double newSegment = distance(a - 1, b) + distance(b, a + 1) + distance(b - 1, a) + distance(a, b + 1);
    return newSegment - oldSegment;
}

void JPTravel::swapLocations(int a, int b, bool twist) {
    if (!twist) {
        std::swap(m_travel[size_t(a)], m_travel[size_t(b)]);
        return;
    }
    if (a > b) std::swap(a, b);
    for (int i = 0; i < (b - a + 1) / 2; i++) std::swap(m_travel[size_t(a + i)], m_travel[size_t(b - i)]);
}

double JPTravel::simulateAnnealing(double startingTemperature, double coolingRate, int maxIterations) {
    const int size = int(m_travel.size());
    double bestDistance = travellingDistance();
    double temperature = startingTemperature;
    // Small variations tried down to this (OpenPnP: a conservative 1/1000 of the start).
    const double endTemperature = startingTemperature / 1000;
    if (size > 1) {
        std::vector<size_t> globalTravel = m_travel;   // the global best route
        double globalBestDistance = kGlobalBestDistanceScalingFactor * bestDistance;
        // Seeded, so the results repeat (OpenPnP PR 1715).
        JPJavaRandom rnd(0);
        for (int i = maxIterations; i > 0; i--) {
            if (temperature <= endTemperature) break;
            const int a = rnd.nextInt(size);
            int b;
            do {
                b = rnd.nextInt(size);
            } while (b == a);
            bool twist = false;
            double swap = swapDistance(a, b, false);
            const double twistDistance = swapDistance(a, b, true);
            // The better of the two.
            if (twistDistance < swap) {
                twist = true;
                swap = twistDistance;
            }
            if (swap < 0.0 || std::exp(-swap / temperature) >= rnd.nextDouble()) {
                // Better, or within the annealing probability.
                swapLocations(a, b, twist);
                bestDistance += swap;
                if (bestDistance < globalBestDistance) {
                    globalBestDistance = kGlobalBestDistanceScalingFactor * bestDistance;
                    globalTravel = m_travel;
                }
            }
            temperature *= coolingRate;
        }
        // The global best route is the best there is.
        m_travel = globalTravel;
    }
    return travellingDistance();
}

double JPTravel::solve() {
    // OpenPnP's heuristic for the annealing.
    const int size = std::max(1, int(m_travel.size()));
    return simulateAnnealing(travellingDistance() / size * 2.0, 1.0 - 0.001 / size, size * 1000 + 10000000);
}

std::vector<size_t> JPTravel::order(const std::vector<JPLocation>& points, const std::optional<JPLocation>& start,
                                    const std::optional<JPLocation>& end, const std::optional<Cost>& cost) {
    JPTravel t(points, start, end, cost);
    t.solve();
    return t.order();
}

} // inline namespace jf
