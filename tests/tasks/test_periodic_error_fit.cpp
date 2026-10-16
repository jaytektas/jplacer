// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// A drive's periodic error from a board scan (JPPeriodicErrorFit): footprints found by a head whose Y drive has a
// 45 mm wave (and the twist it makes across), on a board moved, turned and a little shrunk, with a few hundredths of
// finding noise and two finds on the wrong thing: the wave comes back to within the noise, the board's scale and turn
// too, the two finds left out, and what is left far smaller than without the wave. Too few finds, or no period: no
// fit, and why. A driven X: the wave along X.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "tasks/JPPeriodicErrorFit.h"

#include <cmath>
#include <random>

using namespace jf;

namespace {

std::vector<JPPeriodicErrorFit::Find> finds(bool alongY, double alongS, double alongC, double acrossS, double acrossC) {
    std::mt19937 rng(7);
    std::normal_distribution<double> noise(0, 0.006);
    const double scale = 0.9996, turn = 0.05 * M_PI / 180, tx = 183.7, ty = 68.7, k = 2 * M_PI / 45;
    std::vector<JPPeriodicErrorFit::Find> out;
    // Dense along the driven axis, three rows across it.
    for (double d = 5; d <= 105; d += 4)
        for (double other : { 10.0, 60.0, 100.0 }) {
            const double dx = alongY ? other : d, dy = alongY ? d : other;
            JPPeriodicErrorFit::Find f;
            f.designX = dx;
            f.designY = dy;
            const double px = scale * (std::cos(turn) * dx - std::sin(turn) * dy) + tx;
            const double py = scale * (std::sin(turn) * dx + std::cos(turn) * dy) + ty;
            f.at = alongY ? py : px;
            const double s = std::sin(k * f.at), c = std::cos(k * f.at);
            const double along = alongS * s + alongC * c, across = acrossS * s + acrossC * c;
            // The head that much past where it was sent: what it finds, that much short.
            f.foundX = px - (alongY ? across : along) + noise(rng);
            f.foundY = py - (alongY ? along : across) + noise(rng);
            out.push_back(f);
        }
    return out;
}

bool near(double a, double b, double within) { return std::abs(a - b) <= within; }

} // namespace

int main() {
    std::vector<JPPeriodicErrorFit::Find> f = finds(true, 0.021, -0.009, 0.004, 0.012);
    // Two on the wrong thing: 0.3 mm off.
    f[10].foundY += 0.3;
    f[40].foundX -= 0.3;
    JPPeriodicErrorConfig::Scan scan;
    std::string why;
    assert(JPPeriodicErrorFit::fit(f, 45, true, scan, why));
    assert(near(scan.alongSin, 0.021, 0.004) && near(scan.alongCos, -0.009, 0.004));
    assert(near(scan.acrossSin, 0.004, 0.004) && near(scan.acrossCos, 0.012, 0.004));
    assert(near(scan.scale, 0.9996, 0.0001) && near(scan.rotationDeg, 0.05, 0.005));
    assert(scan.points.size() == f.size() && scan.points[10].outlier && scan.points[40].outlier);
    int left = 0;
    for (const auto& p : scan.points) left += p.outlier;
    assert(left == 2);
    assert(scan.rmsAfter < 0.012 && scan.rmsBefore > 2 * scan.rmsAfter && !scan.applied && scan.periodMm == 45);
    // A point's measured wave and the fit's, alike.
    const auto& p0 = scan.points[0];
    assert(near(p0.along, p0.alongFit, 0.02) && near(p0.across, p0.acrossFit, 0.02));

    // The driven axis X: the wave along X.
    const std::vector<JPPeriodicErrorFit::Find> g = finds(false, 0.015, 0.01, 0.0, -0.008);
    assert(JPPeriodicErrorFit::fit(g, 45, false, scan, why));
    assert(near(scan.alongSin, 0.015, 0.004) && near(scan.alongCos, 0.01, 0.004) && near(scan.acrossCos, -0.008, 0.004));

    // Too few, or no period.
    const std::vector<JPPeriodicErrorFit::Find> few(f.begin(), f.begin() + 5);
    assert(!JPPeriodicErrorFit::fit(few, 45, true, scan, why) && why.find("at least") != std::string::npos);
    assert(!JPPeriodicErrorFit::fit(f, 0, true, scan, why) && why.find("period") != std::string::npos);
    return 0;
}
