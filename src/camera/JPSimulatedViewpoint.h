// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "machine/JPSimulationConfig.h"

#include <chrono>
#include <deque>
#include <mutex>

inline namespace jf {

// Where a simulated head camera really looks, by the cell's Simulation Mode
// (OpenPnP's SimulationModeMachine.getSimulatedPhysicalLocation): from where
// its axes say it is, kept as it goes, the place it had the lag ago, shaken
// after each move, off by the homing error, and skewed by the non-squareness.
class JPSimulatedViewpoint {
public:
    using Clock = std::chrono::steady_clock;

    // `x`, `y`: where the axes say the camera looks now; changed to where it
    // looks in the simulated machine.
    void look(const JPSimulationConfig& sim, Clock::time_point now, double& x, double& y);

private:
    struct Sample {
        Clock::time_point t;
        double            x, y;
    };
    std::mutex         m_mutex;
    std::deque<Sample> m_history;   // where the axes said, as it changed
};

} // inline namespace jf
