// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPMotionProfile.h"

#include <string>
#include <vector>

inline namespace jf {

// OpenPnP's AbstractMotionPath: a sequence of moves (each its axes' profiles, the same axes in the same order),
// optimized into continuous smoothed motion by OpenPnP's simplified "PnP use case" heuristics. Coordinated moves
// (straight lines; OpenPnP's below Safe Z) and uncoordinated ones (within the Safe Zone, free to stray from the
// line):
//  1. Path begin and end, and corners between coordinated moves, are junctions at zero velocity and acceleration.
//  2. Successive co-linear moves are solved as one, with the most restrictive limits; each cut out of it.
//  3. A coordinated move followed by an uncoordinated one overshoots into it: to the axis's limit where it has one
//     (then cropped), else as a half-sided profile (extended or cropped); preceded by one, the same in reverse.
//  4. One both preceded and followed by uncoordinated moves is not optimized.
// Uncoordinated moves then take their neighbours' entry/exit velocity and acceleration, and the overshoot is
// refined over a few iterations where it wastes time.
class JPMotionPath {
public:
    explicit JPMotionPath(std::vector<std::vector<JPMotionProfile>*> moves) : m_moves(std::move(moves)) {}

    void solve(double approximation = kApproximation, int iterations = kIterations);
    // Seamless: each move starts where, as fast and as accelerated as the one before ended. Empty when so, else
    // what is not.
    std::string validate() const;

    static constexpr double kApproximation = 0.75;   // how fast the estimated best solution is approached
    static constexpr int    kIterations = 3;

private:
    static bool controlOvershoot(const std::vector<JPMotionProfile>* prev, const std::vector<JPMotionProfile>& entry,
                                 const std::vector<JPMotionProfile>& exit, const std::vector<JPMotionProfile>* next, int axis,
                                 JPMotionProfile& solver, double approximation);

    std::vector<std::vector<JPMotionProfile>*> m_moves;
};

} // inline namespace jf
