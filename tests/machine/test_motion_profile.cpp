// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// OpenPnP's AdvancedMotionTest.testMotionProfiles, case for case: each profile solved as given, reversed, with
// constant acceleration and with simplified S-curves, and found free of errors but the one expected. And what the
// profiles must be besides: from and to still-stand they end where asked, in the shortest time the limits allow
// (a constant acceleration one checked against the closed form), a minimum time met, coordinated axes in step.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "machine/JPMotionProfile.h"

using namespace jf;

namespace {

using P = JPMotionProfile;
using E = JPMotionProfile::Error;
constexpr double kInf = std::numeric_limits<double>::infinity();

const char* errorName(std::optional<E> e) {
    if (!e) return "none";
    static const char* names[] = { "SolutionNotFinite",     "NegativeSegmentTime",     "TimeSumMismatch",     "LocationDiscontinuity",
                                   "VelocityDiscontinuity", "AccelerationDiscontinuity", "MinTimeViolated",   "MaxTimeViolated",
                                   "MinLocationViolated",   "MaxLocationViolated",     "MaxVelocityViolated", "MaxAccelerationViolated",
                                   "MaxJerkViolated" };
    return names[int(*e)];
}

void testCase(const std::string& message, P profile, std::optional<E> expected) {
    profile.solve();
    const auto error = profile.checkValidity();
    if (error && error != expected) {
        std::fprintf(stderr, "%s has error %s (time %.6f)\n", message.c_str(), errorName(error), profile.time());
        assert(false);
    }
}

// As OpenPnP's testProfile: the profile, reversed, with constant acceleration, and with simplified S-curves.
void testProfile(const std::string& message, const P& p, std::optional<E> expected) {
    const P reversed(p.location(P::kSegments), p.location(0), -p.velocity(P::kSegments), -p.velocity(0), p.acceleration(P::kSegments),
                     p.acceleration(0), p.locationMin(), p.locationMax(), p.velocityMax(), p.entryAccelerationMax(),
                     p.exitAccelerationMax(), p.jerkMax(), p.timeMin(), p.timeMax(), p.options());
    const P constantAcc(p.location(0), p.location(P::kSegments), p.velocity(0), p.velocity(P::kSegments), 0, 0, p.locationMin(),
                        p.locationMax(), p.velocityMax(), p.entryAccelerationMax(), p.exitAccelerationMax(), 0, p.timeMin(),
                        p.timeMax(), p.options());
    const P simpleSCurve(p.location(0), p.location(P::kSegments), p.velocity(0), p.velocity(P::kSegments), 0, 0, p.locationMin(),
                         p.locationMax(), p.velocityMax(), p.entryAccelerationMax(), p.exitAccelerationMax(), p.jerkMax(),
                         p.timeMin(), p.timeMax(), p.options() | P::flag(P::SimplifiedSCurve));
    testCase(message, p, expected);
    testCase(message + " (reverse)", reversed, expected);
    testCase(message + " (constant acceleration)", constantAcc, expected);
    testCase(message + " (simplified S-Curve)", simpleSCurve, expected);
}

P make(double s0, double s1, double v0, double v1, double a0, double a1, double vMax, double tMin, int options = 0,
       double aEntry = 2000, double aExit = 2000, double jerk = 15000) {
    return P(s0, s1, v0, v1, a0, a1, 0, 1000, vMax, aEntry, aExit, jerk, tMin, kInf, options);
}

P solved(P p) {
    p.solve();
    return p;
}

} // namespace

int main() {
    // OpenPnP's cases.
    testProfile("Long move from/to still-stand", make(0, 600, 0, 0, 0, 0, 700, 0), std::nullopt);
    testProfile("Short move from/to still-stand", make(0, 200, 0, 0, 0, 0, 700, 0), std::nullopt);
    testProfile("Low feed-rate from/to still-stand", make(0, 200, 0, 0, 0, 0, 100, 0), std::nullopt);
    testProfile("Tiny move from/to still-stand", make(0, 10, 0, 0, 0, 0, 700, 0), std::nullopt);
    testProfile("Micro move from/to still-stand", make(0, 0.0001, 0, 0, 0, 0, 700, 0), std::nullopt);
    testProfile("Short move with unconstrained exit", make(0, 100, 0, 0, 0, 0, 700, 0, P::flag(P::UnconstrainedExit)), std::nullopt);
    testProfile("Short move with unconstrained entry", make(0, 100, 0, 0, 0, 0, 700, 0, P::flag(P::UnconstrainedEntry)), std::nullopt);
    testProfile("Tiny move with unconstrained exit", make(0, 10, 0, 0, 0, 0, 700, 0, P::flag(P::UnconstrainedExit)), std::nullopt);
    testProfile("Tiny move with unconstrained entry", make(0, 10, 0, 0, 0, 0, 700, 0, P::flag(P::UnconstrainedEntry)), std::nullopt);
    testProfile("Move with max entry/exit velocity", make(0, 100, 700, 700, 0, 0, 700, 0), std::nullopt);
    testProfile("Move with lower entry/exit velocity", make(0, 100, 200, 200, 0, 0, 700, 0), std::nullopt);
    testProfile("Move with entry/exit velocity and min time", make(0, 400, 500, 300, 0, 0, 700, 2), std::nullopt);
    testProfile("Move with entry/exit velocity/acceleration", make(0, 400, 700, -700, 2000, 2000, 700, 0), E::MaxVelocityViolated);
    const P problem = solved(make(100, 110, 200, 200, 2000, 2000, 700, 0));
    testProfile("Problem move with entry/exit velocity/acceleration", problem, std::nullopt);
    testProfile("Problem move with entry/exit velocity/acceleration and min-time", make(100, 110, 200, 200, 2000, 2000, 700, problem.time() + 0.1),
                std::nullopt);
    testProfile("Still-stand", make(0, 0, 0, 0, 0, 0, 700, 0), std::nullopt);
    testProfile("Still-stand with min-time", make(0, 0, 0, 0, 0, 0, 700, 4.0), std::nullopt);
    testProfile("Zero displacement move with entry/exit velocity/acceleration", make(0, 0, 700, 700, 2000, 2000, 700, 0), std::nullopt);
    const P overshoot = solved(make(0, 0, 700, -700, 2000, 2000, 700, 0));
    testProfile("Overshoot", overshoot, std::nullopt);
    testProfile("Overshoot slightly prolonged", make(0, 0, 700, -700, 2000, 2000, 700, overshoot.time() + 0.01), std::nullopt);
    testProfile("Z axis in moveToLoactionAtSafeZ() with min time", make(0, 0, 200, -200, 2000, 2000, 700, 4.0), std::nullopt);
    testProfile("Z axis in asymmetric moveToLoactionAtSafeZ() with min time", make(0, 0, 200, -100, 1000, 1750, 700, 4.0), std::nullopt);
    testProfile("Z axis in more asymmetric moveToLoactionAtSafeZ() with min time", make(0, 0, 200, -50, 1000, 500, 700, 4.0), std::nullopt);
    testProfile("Twisted curve (min-time)", make(0, 0, 500, 500, 0, 0, 700, 4.0), E::MinLocationViolated);
    testProfile("Twisted curve (min-time) with delta", make(0, 10, 500, 500, 0, 0, 700, 4.0), E::MinLocationViolated);
    const P complex = solved(make(0, 400, 0, -20, 0, -100, 700, 4.0));
    testProfile("Complex move with min-time", complex, std::nullopt);
    // The solution's own limits, no minimum time: solved again, the same.
    const JPMotionControlType full = JPMotionControlType::Full3rdOrderControl;
    testProfile("recheck solution",
                P(0, 400, 0, -20, 0, -100, 0, 1000, complex.profileVelocity(full), complex.profileAcceleration(full),
                  complex.profileAcceleration(full), complex.profileJerk(full), 0.0, kInf, 0),
                std::nullopt);

    // From and to still-stand: it ends where asked, at rest, within its limits.
    {
        const P p = solved(make(0, 600, 0, 0, 0, 0, 700, 0));
        assert(std::abs(p.momentaryLocation(p.time()) - 600) < 1e-6 && std::abs(p.momentaryVelocity(p.time())) < 1e-6);
        assert(p.higherVBoundary() <= 700 + P::kVtol && p.higherABoundary() <= 2000 + P::kAtol);
        // Long enough to cruise: v reaches 700.
        assert(std::abs(p.higherVBoundary() - 700) < 1e-6);
    }
    // Constant acceleration, from and to rest: the closed form's time.
    {
        const P p = solved(make(0, 600, 0, 0, 0, 0, 700, 0, 0, 2000, 2000, 0));
        const double t = 600.0 / 700 + 700.0 / 2000;   // cruise, plus the time to reach and leave 700 at 2000
        assert(std::abs(p.time() - t) < 1e-6);
        const P shortMove = solved(make(0, 100, 0, 0, 0, 0, 700, 0, 0, 2000, 2000, 0));
        assert(std::abs(shortMove.time() - 2 * std::sqrt(100.0 / 2000)) < 1e-6);   // a triangle
    }
    // A minimum time is met exactly.
    {
        const P p = solved(make(0, 100, 0, 0, 0, 0, 700, 2.0));
        assert(std::abs(p.time() - 2.0) < 1e-6 && !p.checkValidity());
    }
    // Coordinated: the axes in step, each scaled from the lead.
    {
        std::vector<P> axes { make(0, 300, 0, 0, 0, 0, 700, 0, P::flag(P::Coordinated)), make(0, 100, 0, 0, 0, 0, 700, 0, P::flag(P::Coordinated)) };
        P::coordinateProfiles(axes);
        assert(std::abs(axes[0].time() - axes[1].time()) < 1e-12);
        for (double t = 0; t < axes[0].time(); t += axes[0].time() / 17)
            assert(std::abs(axes[1].momentaryLocation(t) - axes[0].momentaryLocation(t) / 3) < 1e-6);
    }
    // Synchronized: each its own, all taking the longest one's time.
    {
        std::vector<P> axes { make(0, 300, 0, 0, 0, 0, 700, 0), make(0, 10, 0, 0, 0, 0, 700, 0) };
        P::synchronizeProfiles(axes);
        assert(std::abs(axes[0].time() - axes[1].time()) < 1e-6 && !axes[1].checkValidity());
        assert(std::abs(axes[1].momentaryLocation(axes[1].time()) - 10) < 1e-6);
    }
    return 0;
}
