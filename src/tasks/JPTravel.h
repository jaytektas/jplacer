// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "model/JPLocation.h"

#include <optional>
#include <vector>

inline namespace jf {

// OpenPnP's TravellingSalesman: an order through some places from `start` (where given) and towards `end` (where
// given), found by OpenPnP's simulated annealing: random pairs of places swapped, or the stretch between them
// reversed (whichever is better), a worse order taken now and then while the temperature is high, cooling as it
// goes, the best order kept. Seeded as OpenPnP's, so it gives the same order for the same places. The cost of a hop
// is OpenPnP's TravelCost (the time the X, Y and Z axes take from standstill to standstill at their feed rates and
// accelerations, the slowest) where given, else the straight XYZ distance.
class JPTravel {
public:
    // OpenPnP's TravelCost: the axes a hop is timed by (mm/s, mm/s^2), Z where it is a controller's axis.
    struct Axis {
        double feedrate = 0, acceleration = 0;
    };
    struct Cost {
        Axis x, y;
        std::optional<Axis> z;
        double xyzCost(const JPLocation& a, const JPLocation& b) const;
    };

    JPTravel(std::vector<JPLocation> points, std::optional<JPLocation> start, std::optional<JPLocation> end,
             std::optional<Cost> cost = std::nullopt);

    void setCost(std::optional<Cost> cost) { m_cost = std::move(cost); }
    // The route's cost as it stands (its time with a cost, else its length), from the start to the end where given.
    double travellingDistance() const;
    // OpenPnP's solve: annealed with its heuristic settings; the route's cost after.
    double solve();
    double simulateAnnealing(double startingTemperature, double coolingRate, int maxIterations);
    // The places' indices in the route's order.
    std::vector<size_t> order() const { return m_travel; }

    // The order solved, at once.
    static std::vector<size_t> order(const std::vector<JPLocation>& points, const std::optional<JPLocation>& start,
                                     const std::optional<JPLocation>& end, const std::optional<Cost>& cost = std::nullopt);

private:
    const JPLocation* location(int i) const;   // -1: the start, size: the end, else the route's i'th
    double distance(int a, int b) const;
    double swapDistance(int a, int b, bool twist) const;
    void   swapLocations(int a, int b, bool twist);

    std::vector<JPLocation>   m_points;   // in mm
    std::optional<JPLocation> m_start, m_end;
    std::optional<Cost>       m_cost;
    std::vector<size_t>       m_travel;   // the route: indices into m_points
};

} // inline namespace jf
