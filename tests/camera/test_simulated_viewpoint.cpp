// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// OpenPnP's Simulation Mode on what a head camera sees: none on an ideal
// machine; a homing error and non-squareness on an imperfect one; and on a
// dynamically imperfect one, the camera's lag and its shaking after a move,
// dying away over the duration.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "camera/JPSimulatedViewpoint.h"

#include <cmath>

using namespace jf;
using Clock = JPSimulatedViewpoint::Clock;

namespace {

Clock::time_point at(Clock::time_point t0, double s) {
    return t0 + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(s));
}

} // namespace

int main() {
    const auto t0 = Clock::now();
    // Ideal: as the axes say.
    {
        JPSimulationConfig sim;
        sim.mode = JPSimulationConfig::Mode::IdealMachine;
        sim.homingErrorX = 1;
        JPSimulatedViewpoint v;
        double x = 10, y = 20;
        v.look(sim, t0, x, y);
        assert(x == 10 && y == 20);
    }
    // Static: off by the homing error, X leaning with Y; no lag.
    {
        JPSimulationConfig sim;
        sim.mode = JPSimulationConfig::Mode::StaticImperfectionsMachine;
        sim.homingErrorX = 0.2;
        sim.homingErrorY = -0.1;
        sim.nonSquarenessFactor = 0.001;
        sim.cameraLagS = 5;
        JPSimulatedViewpoint v;
        double x = 10, y = 100;
        v.look(sim, t0, x, y);
        assert(std::abs(y - 100.1) < 1e-12 && std::abs(x - (9.8 + 0.001 * 100.1)) < 1e-12);
    }
    // Dynamic: what it showed the lag ago, then shaking along the move.
    {
        JPSimulationConfig sim;
        sim.mode = JPSimulationConfig::Mode::DynamicImperfectionsMachine;
        sim.cameraLagS = 0.1;
        sim.vibrationAmplitudeMm = 0.05;
        sim.vibrationDurationS = 0.4;
        JPSimulatedViewpoint v;
        double x = 0, y = 0;
        v.look(sim, t0, x, y);
        x = 10, y = 0;   // moved to X 10 at 1 s
        v.look(sim, at(t0, 1.0), x, y);
        assert(x == 0 && y == 0);   // not seen yet: the lag
        x = 10, y = 0;
        v.look(sim, at(t0, 1.1), x, y);   // the move seen, just now: shaken back along it
        assert(std::abs(x - (10 - 0.05)) < 1e-9 && y == 0);
        x = 10, y = 0;
        v.look(sim, at(t0, 1.1 + 1 / (2 * JPSimulationConfig::kVibrationHz)), x, y);   // half a swing on: past it
        assert(x > 10);
        x = 10, y = 0;
        v.look(sim, at(t0, 1.6), x, y);   // the shaking over
        assert(x == 10);
    }
    return 0;
}
