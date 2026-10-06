// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// OpenPnP's AdvancedMotionTest.testMotionPaths: pick and place paths (one nozzle and dual, symmetric and
// asymmetric, and a move to a push/pull feeder) of coordinated moves below Safe Z and uncoordinated ones within it,
// with jerk control (90000 and 30000 mm/s^3, plain and simplified S-curves) and constant acceleration, optimized
// into continuous motion (JPMotionPath) and found seamless and within every limit, as OpenPnP's validate() checks.
// And what the optimizing is for: the path no slower than its moves one by one, faster where uncoordinated moves
// let it blend.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "machine/JPMotionPath.h"

using namespace jf;

namespace {

using P = JPMotionProfile;
constexpr double kInf = std::numeric_limits<double>::infinity();
constexpr double kSafeZ = -7, kZa = -15, kZb = -13;

// As OpenPnP's PlannerPath: moves added between waypoints, each solved as a coordinated move.
struct PlannerPath {
    double jerk;
    bool   sCurves;
    std::vector<std::unique_ptr<std::vector<P>>> path;
    std::optional<double> x0, y0, z0;

    void moveTo(double x, double y, double z, int nozzle) {
        if (x0 && y0 && z0) {
            int options = sCurves ? P::flag(P::SimplifiedSCurve) : 0;
            double zMin, zMax;
            const bool inSafeZone = (*z0 >= kSafeZ && *z0 <= -kSafeZ) && (z >= kSafeZ && z <= -kSafeZ);
            if (nozzle == 1) {
                zMin = -20;
                zMax = 5;
            } else {
                zMin = -5;
                zMax = 20;
            }
            if (inSafeZone && nozzle != 0) {
                zMin = kSafeZ;
                zMax = -kSafeZ;
                options |= P::flag(P::SynchronizeStraighten) | P::flag(P::SynchronizeEarlyBird) | P::flag(P::SynchronizeLastMinute);
            } else {
                options |= P::flag(P::Coordinated);
            }
            auto profiles = std::make_unique<std::vector<P>>();
            profiles->push_back(P(*x0, x, 0, 0, 0, 0, 0, 1000, 700, 2000, 2000, jerk, 0, kInf, options));
            profiles->push_back(P(*y0, y, 0, 0, 0, 0, 0, 500, 700, 2000 / 2, 2000 / 2, jerk, 0, kInf, options));
            profiles->push_back(P(*z0, z, 0, 0, 0, 0, zMin, zMax, 700, 2000, 2000, jerk, 0, kInf, options));
            // Solved as a single coordinated move.
            const int lead = P::leadAxisIndex(P::unitVector(*profiles));
            (*profiles)[size_t(lead)].solve();
            P::coordinateProfiles(*profiles);
            path.push_back(std::move(profiles));
        }
        x0 = x;
        y0 = y;
        z0 = z;
    }

    double overallTime() const {
        double t = 0;
        for (const auto& p : path) t += (*p)[0].time();
        return t;
    }
    JPMotionPath motionPath() {
        std::vector<std::vector<P>*> moves;
        for (auto& p : path) moves.push_back(p.get());
        return JPMotionPath(moves);
    }
};

void pickAndPlace(PlannerPath& path) {
    // pick & place, one nozzle, symmetric
    for (double x : { 0.0, 100.0, 120.0, 124.0, 125.0 }) {
        path.moveTo(x, 0, kSafeZ, 1);
        path.moveTo(x, 0, kZa, 1);
        path.moveTo(x, 0, kSafeZ, 1);
    }
    // pick & place, one nozzle, asymmetric
    const double zs[] = { kZb, kZa, kZb, kZa, kZb };
    int k = 0;
    for (double x : { 0.0, 100.0, 120.0, 124.0, 125.0 }) {
        path.moveTo(x, 50, kSafeZ, 1);
        path.moveTo(x, 50, zs[k++], 1);
        path.moveTo(x, 50, kSafeZ, 1);
    }
    // pick & place, dual nozzle, symmetric and asymmetric
    for (double y : { 100.0, 150.0 }) {
        path.moveTo(0, y, kSafeZ, 1);
        path.moveTo(0, y, kZa, 1);
        path.moveTo(0, y, kSafeZ, 1);
        path.moveTo(100, y, -kSafeZ, 2);
        path.moveTo(100, y, 15, 2);
        path.moveTo(100, y, -kSafeZ, 2);
        path.moveTo(120, y, kSafeZ, 1);
        path.moveTo(120, y, y == 100 ? kZa : kZb, 1);
        path.moveTo(120, y, kSafeZ, 1);
        path.moveTo(124, y, -kSafeZ, 2);
        path.moveTo(124, y, 15, 2);
        path.moveTo(124, y, -kSafeZ, 2);
        path.moveTo(125, y, kSafeZ, 1);
        path.moveTo(125, y, y == 100 ? kZa : kZb, 1);
        path.moveTo(125, y, kSafeZ, 1);
    }
    // move to push/pull feeder
    path.moveTo(200, 50, kSafeZ, 1);
    path.moveTo(220, 50, kSafeZ - 5, 1);
    path.moveTo(220, 50, kSafeZ, 1);
    path.moveTo(200, 80, kSafeZ, 1);
    path.moveTo(190, 100, kSafeZ, 1);
    path.moveTo(190, 120, kSafeZ, 1);
    path.moveTo(300, 120, kZa, 1);
    path.moveTo(300, 150, kZa, 1);
    path.moveTo(280, 150, kZa, 1);
    path.moveTo(279, 150, kZa, 1);
    path.moveTo(275, 150, kZa, 1);
    path.moveTo(275, 150, kSafeZ, 1);
}

} // namespace

int main() {
    for (auto [jerk, sCurves] : { std::pair { 90000.0, false }, std::pair { 90000.0, true }, std::pair { 30000.0, false },
                                  std::pair { 30000.0, true }, std::pair { 0.0, false }, std::pair { 0.0, true } }) {
        PlannerPath path { jerk, sCurves, {}, {}, {}, {} };
        pickAndPlace(path);
        const double unoptimized = path.overallTime();
        JPMotionPath mp = path.motionPath();
        mp.solve();
        const std::string error = mp.validate();
        const double optimized = path.overallTime();
        std::fprintf(stderr, "jerk %g%s: total move time %.3f s, unoptimized %.3f s, %+.2f %%%s%s\n", jerk,
                     sCurves ? " (simplified S-curves)" : "", optimized, unoptimized, 100 * (optimized / unoptimized - 1),
                     error.empty() ? "" : ": ", error.c_str());
        // OpenPnP's validate(): seamless and within the limits.
        assert(error.empty());
        // Optimized, no slower than one by one.
        assert(optimized <= unoptimized + 1e-6);
    }
    return 0;
}
