// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// OpenPnP's runout fits (ReferenceNozzleTipCalibration) from a tip's measured offsets: a known circle (its centre,
// radius and phase) measured at OpenPnP's angles, with a little noise, found by the Kasa circle fit and by the
// affine fit onto a 1 mm runout alike; a tip with no runout (every offset the same) as OpenPnP takes it; a table
// interpolated between its angles (and past the last, towards the first); what each algorithm sends a move and
// takes the camera's offset to be; and too few measurements, or an algorithm OpenPnP does not have, refused.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "tasks/JPRunoutFit.h"

#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

using namespace jf;

namespace {

constexpr double kCx = 0.07, kCy = -0.05, kRadius = 0.3, kPhase = -40;

std::vector<JPRunout::Point> measured(int divisions, double noise) {
    std::mt19937 rng(7);
    std::normal_distribution<double> n(0, noise);
    std::vector<JPRunout::Point> points;
    for (int i = 0; i < divisions; ++i) {
        const double a = -180 + 360.0 * i / divisions, t = (a - kPhase) * M_PI / 180;
        points.push_back({ a, kCx + kRadius * std::cos(t) + n(rng), kCy + kRadius * std::sin(t) + n(rng) });
    }
    return points;
}

bool near(double a, double b, double tol) { return std::abs(a - b) <= tol; }

} // namespace

int main() {
    const auto points = measured(6, 0.002);
    for (const std::string& algorithm : JPRunout::algorithms()) {
        const auto r = JPRunoutFit::fit(points, algorithm);
        assert(r && r->algorithm == algorithm && r->points.size() == 6);
        if (r->table()) continue;
        std::fprintf(stderr, "%s: centre %.4f, %.4f, radius %.4f, phase %.2f, rms %.4f\n", algorithm.c_str(), r->centreX,
                     r->centreY, r->radius, r->phaseDeg, r->rmsMm);
        assert(near(r->centreX, kCx, 0.003) && near(r->centreY, kCy, 0.003) && near(r->radius, kRadius, 0.004));
        assert(near(r->phaseDeg, kPhase, 1.0) && r->rmsMm < 0.005 && r->peakMm >= r->rmsMm);
        // What a move is sent the other way, and the camera's offset.
        double dx, dy, sx, sy, cx, cy;
        r->offset(30, dx, dy);
        r->runoutAt(30, sx, sy);
        r->cameraOffset(cx, cy);
        const bool model = algorithm == "Model" || algorithm == "ModelAffine";
        const bool camera = algorithm.rfind("ModelCameraOffset", 0) == 0;
        assert(near(dx, sx + (model ? r->centreX : 0), 1e-12) && near(dy, sy + (model ? r->centreY : 0), 1e-12));
        assert(near(cx, camera ? r->centreX : 0, 1e-12) && near(cy, camera ? r->centreY : 0, 1e-12));
    }

    // No runout at all (a simulated machine's): OpenPnP's Kasa fit takes the offset as it is, radius 0.
    {
        const std::vector<JPRunout::Point> same { { -180, 0.1, 0.2 }, { -60, 0.1, 0.2 }, { 60, 0.1, 0.2 } };
        const auto r = JPRunoutFit::fit(same, "Model");
        assert(r && r->radius == 0 && r->centreX == 0.1 && r->centreY == 0.2);
        const auto a = JPRunoutFit::fit(same, "ModelAffine");
        assert(a && a->radius == 0 && near(a->centreX, 0.1, 1e-9) && near(a->centreY, 0.2, 1e-9));
    }

    // A table: at its angles the offsets measured, between them in proportion, past the last towards the first.
    {
        const auto t = JPRunoutFit::fit(points, "Table");
        double dx, dy;
        t->offset(points[2].angle, dx, dy);
        assert(dx == points[2].dx && dy == points[2].dy);
        t->offset((points[1].angle + points[2].angle) / 2, dx, dy);
        assert(near(dx, (points[1].dx + points[2].dx) / 2, 1e-12) && near(dy, (points[1].dy + points[2].dy) / 2, 1e-12));
        t->offset(points.back().angle + 360, dx, dy);   // the same angle a turn on
        assert(near(dx, points.back().dx, 1e-12) && near(dy, points.back().dy, 1e-12));
        double cx, cy;
        t->cameraOffset(cx, cy);
        assert(cx == 0 && cy == 0);
    }

    // Too few, or an algorithm OpenPnP does not have.
    assert(!JPRunoutFit::fit(measured(2, 0), "Model"));
    assert(!JPRunoutFit::fit(points, "Circle"));
    // Kept as measured, and read back.
    const auto r = JPRunoutFit::fit(points, "ModelCameraOffset");
    const JPRunout back = JPRunout::fromJson(r->toJson());
    assert(back.algorithm == "ModelCameraOffset" && back.radius == r->radius && back.points.size() == r->points.size());
    // One kept before there was a choice: as it was used, the swing alone.
    JJson old = r->toJson();
    std::erase_if(old.obj(), [](const auto& kv) { return kv.first == "algorithm"; });
    assert(JPRunout::fromJson(old).algorithm == JPRunout::kKeptAlgorithm);
    return 0;
}
