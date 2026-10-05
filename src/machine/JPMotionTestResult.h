// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <map>
#include <string>
#include <utility>
#include <vector>

inline namespace jf {

// What a motion planner Test Motion run (JPCell::testMotionAndWait) found:
// how long its moves were planned to take, from the axes' feed rates and
// accelerations (a move as fast as its slowest axis allows, speeding up and
// slowing down at its acceleration), how long they took, and each moving
// axis's place over the run as the controllers reported it, by axis name:
// (seconds from the start, coordinate).
struct JPMotionTestResult {
    double plannedS = 0;
    double actualS = 0;
    std::map<std::string, std::vector<std::pair<double, double>>> axes;
};

} // inline namespace jf
