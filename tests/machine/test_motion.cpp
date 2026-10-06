// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// OpenPnP's Motion for each Motion Control Type: a coordinated move's feed rate over the linear axes as G-code reads
// F (RS274NGC), what each type tells the controller, the moderated constant acceleration move taking the planned
// time, and the interpolated simulation of jerk control: waypoints along the move to its end, the acceleration
// changing in steps no larger than allowed, the steps adding up to the planned time. Uncoordinated: each axis in
// its own limits, all ending together.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

#include "machine/JPMotion.h"

using namespace jf;

namespace {

using T = JPMotionControlType;

std::vector<JPMotion::Axis> axes(double jerk) {
    // X and Y: 500 mm/s, 2000 mm/s^2, the jerk given; Z slower; C (rotation) 3600 deg/s.
    JPMotion::Axis x { "X", "D", false, { 0, 500, 2000, jerk } };
    JPMotion::Axis y { "Y", "D", false, { 0, 500, 2000, jerk } };
    JPMotion::Axis z { "Z", "D", false, { 0, 100, 1000, jerk } };
    JPMotion::Axis c { "C", "D", true, { 0, 3600, 20000, 0 } };
    return { x, y, z, c };
}

JPMotion::Driver driver(T type, double feedRatePerSecond = 0) {
    JPMotion::Driver d;
    d.id = "D";
    d.type = type;
    d.feedRatePerSecond = feedRatePerSecond;
    d.minimumRate[1] = 1;
    d.minimumRate[2] = 10;
    d.minimumRate[3] = 100;
    return d;
}

bool near(double a, double b, double tol) { return std::abs(a - b) <= tol; }

} // namespace

int main() {
    const JPMotion::Location home { { "X", 0 }, { "Y", 0 }, { "Z", 0 }, { "C", 0 } };
    const JPMotion::Location to { { "X", 300 }, { "Y", 400 }, { "Z", 0 }, { "C", 0 } };   // 500 mm diagonally

    // EuclideanAxisLimits: the axes' limits, normed to the path: X and Y each limited to 500 mm/s, along a 3-4-5
    // diagonal Y limits it, so the path goes at 500*5/4 = 625 mm/s; told as F over the linear axes.
    {
        const JPMotion::Driver d = driver(T::EuclideanAxisLimits);
        JPMotion m(axes(0), { d }, home, to, 1.0, 0);
        const auto moves = m.interpolatedMoveToCommands(d, true);
        assert(moves.size() == 1);
        assert(moves[0].feedRatePerSecond && near(*moves[0].feedRatePerSecond, 625, 1e-6));
        assert(moves[0].accelerationPerSecond2 && near(*moves[0].accelerationPerSecond2, 2500, 1e-6));
        assert(moves[0].moved.size() == 2 && moves[0].moved.at("X") == 300 && moves[0].moved.at("Y") == 400);
        // At half speed: the feed rate halved, the acceleration quartered (the move the same, stretched in time).
        JPMotion half(axes(0), { d }, home, to, 0.5, 0);
        const auto h = half.interpolatedMoveToCommands(d, true);
        assert(near(*h[0].feedRatePerSecond, 312.5, 1e-6) && near(*h[0].accelerationPerSecond2, 625, 1e-6));
    }
    // ToolpathFeedRate: the controller's own feed rate times the speed, no acceleration.
    {
        const JPMotion::Driver d = driver(T::ToolpathFeedRate, 200);
        JPMotion m(axes(0), { d }, home, to, 0.5, 0);
        const auto moves = m.interpolatedMoveToCommands(d, true);
        assert(moves.size() == 1 && near(*moves[0].feedRatePerSecond, 100, 1e-9));
        assert(!moves[0].accelerationPerSecond2 && !moves[0].jerkPerSecond3);
    }
    // ConstantAcceleration: what the planned profile reaches. 500 mm at 625 mm/s and 2500 mm/s^2: it cruises.
    {
        const JPMotion::Driver d = driver(T::ConstantAcceleration);
        JPMotion m(axes(0), { d }, home, to, 1.0, 0);
        assert(near(m.time(), 500.0 / 625 + 625.0 / 2500, 1e-6));
        const auto moves = m.interpolatedMoveToCommands(d, true);
        assert(near(*moves[0].feedRatePerSecond, 625, 1e-3) && near(*moves[0].accelerationPerSecond2, 2500, 1e-3));
        // A short move does not reach the feed rate: told the peak it does reach.
        const JPMotion::Location shortTo { { "X", 3 }, { "Y", 4 }, { "Z", 0 }, { "C", 0 } };
        JPMotion s(axes(0), { d }, home, shortTo, 1.0, 0);
        const double peak = std::sqrt(5.0 * 2500);   // a triangle: v^2 = s*a
        assert(near(*s.interpolatedMoveToCommands(d, true)[0].feedRatePerSecond, peak, 1e-3));
    }
    // A rotation alone: F over the rotational axes (nothing linear moves).
    {
        const JPMotion::Driver d = driver(T::EuclideanAxisLimits);
        const JPMotion::Location turn { { "X", 0 }, { "Y", 0 }, { "Z", 0 }, { "C", 90 } };
        JPMotion m(axes(0), { d }, home, turn, 1.0, 0);
        assert(near(*m.interpolatedMoveToCommands(d, true)[0].feedRatePerSecond, 3600, 1e-6));
    }

    // Jerk controlled moves.
    const double jerk = 20000;
    // ModeratedConstantAcceleration: one constant acceleration move taking the 3rd order move's time.
    {
        const JPMotion::Driver d = driver(T::ModeratedConstantAcceleration);
        JPMotion m(axes(jerk), { d }, home, to, 1.0, 0);
        const auto moves = m.interpolatedMoveToCommands(d, true);
        assert(moves.size() == 1 && moves[0].time && near(*moves[0].time, m.time(), 1e-9));
        // A trapezoid of that time over 500 mm at that rate and acceleration.
        const double v = *moves[0].feedRatePerSecond, a = *moves[0].accelerationPerSecond2;
        assert(near(v * m.time() - v * v / a, 500, 1e-3));
    }
    // Simulated3rdOrderControl: interpolated into constant acceleration steps.
    {
        JPMotion::Driver d = driver(T::Simulated3rdOrderControl);
        JPMotion m(axes(jerk), { d }, home, to, 1.0, 0);
        const auto moves = m.interpolatedMoveToCommands(d, true);
        assert(moves.size() > 4 && !m.hasOption(JPMotion::InterpolationFailed));
        // The waypoints go along the move, to its end.
        double lastX = 0, timeSum = 0;
        for (const auto& mv : moves) {
            assert(mv.moved.count("X") && mv.moved.at("X") >= lastX - 1e-9);
            assert(near(mv.moved.at("Y") / mv.moved.at("X"), 4.0 / 3, 1e-6));   // on the straight line
            lastX = mv.moved.at("X");
            timeSum += *mv.time;
        }
        assert(near(lastX, 300, 1e-6) && near(moves.back().moved.at("Y"), 400, 1e-6));
        // Retimed: the steps take the planned time.
        assert(near(timeSum, m.time(), 1e-6));
        // The acceleration ramps up in steps: the first step's lower than the move's most.
        double most = 0;
        for (const auto& mv : moves) most = std::max(most, *mv.accelerationPerSecond2);
        assert(*moves[1].accelerationPerSecond2 < most);
        // Too few steps allowed: degraded to one moderated move, said so.
        d.interpolationMaxSteps = 3;
        JPMotion few(axes(jerk), { d }, home, to, 1.0, 0);
        assert(few.interpolatedMoveToCommands(d, true).size() == 1 && few.hasOption(JPMotion::InterpolationFailed));
    }
    // Uncoordinated: each axis in its own limits (Z the slowest), synchronized to end together.
    {
        const JPMotion::Driver d = driver(T::Full3rdOrderControl);
        const JPMotion::Location up { { "X", 100 }, { "Y", 0 }, { "Z", 20 }, { "C", 0 } };
        JPMotion m(axes(jerk), { d }, home, up, 1.0, JPMotion::flag(JPMotion::UncoordinatedMotion));
        for (const JPMotionProfile& p : m.profiles()) {
            assert(near(p.time(), m.time(), 1e-6) && !p.checkValidity());
        }
        const auto end = m.momentaryLocation(m.time());
        assert(near(end.at("X"), 100, 1e-6) && near(end.at("Z"), 20, 1e-6));
    }
    return 0;
}
