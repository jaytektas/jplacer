// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// A nozzle's placement check (JPPlacementCheck): placements made of a known offset error at the place height, a
// part turning with the angle, bottom vision's bias and a drift give them back. Where the object truly sits on the
// nozzle turns with the angle, so bottom vision's offsets average to its bias; each landed error is
//   offset error + turning part (turned by the angle) - vision bias + drift,
// so the bias is their mean, the turning part what turns, and the offset error the bias less the drift plus the
// vision bias. Nothing left over. Kept in the nozzle's file and read back.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "machine/JPNozzleConfig.h"

#include <cmath>

using namespace jf;

namespace {

bool near(double a, double b) { return std::abs(a - b) < 1e-9; }

} // namespace

int main() {
    const double ex = 0.02, ey = -0.03;         // the nozzle's offset error at the place height
    const double vx = 0.015, vy = 0.005;        // turning with the angle (runout, the rotation's centre)
    const double bx = -0.01, by = -0.1;         // bottom vision's bias
    const double dx = 0.004, dy = 0.002;        // the drift
    const double sx = 0.07, sy = -0.12;         // where the object truly sits on the nozzle, at angle 0
    JPPlacementCheck c;
    c.when = "2026-10-15 15:30";
    c.z = -23.8;
    c.drifts = { { dx, dy }, { dx, dy }, { dx, dy } };
    for (int round = 0; round < 2; ++round)
        for (const double angle : { 0.0, 90.0, 180.0, 270.0 }) {
            const double a = angle * M_PI / 180, cs = std::cos(a), sn = std::sin(a);
            const double tx = vx * cs - vy * sn, ty = vx * sn + vy * cs;
            const double ox = sx * cs - sy * sn + bx, oy = sx * sn + sy * cs + by;
            c.places.push_back({ angle, ox, oy, ex + tx - bx + dx, ey + ty - by + dy });
        }
    assert(near(c.biasX(), ex - bx + dx) && near(c.biasY(), ey - by + dy));
    assert(near(c.driftX(), dx) && near(c.driftY(), dy));
    assert(near(c.visionBiasX(), bx) && near(c.visionBiasY(), by));
    assert(near(c.offsetErrorX(), ex) && near(c.offsetErrorY(), ey));
    double tx = 0, ty = 0;
    c.turning(tx, ty);
    assert(near(tx, vx) && near(ty, vy));
    assert(c.worstResidual() < 1e-9);

    JPNozzleConfig n;
    n.id = "N";
    n.placementCheck = c;
    const JPNozzleConfig back = JPNozzleConfig::fromJson(n.toJson());
    assert(back.placementCheck && back.placementCheck->when == c.when && back.placementCheck->z == -23.8);
    assert(back.placementCheck->drifts.size() == 3 && back.placementCheck->places.size() == 8);
    assert(near(back.placementCheck->offsetErrorY(), ey) && near(back.placementCheck->places[5].angle, 90));
    assert(!JPNozzleConfig::fromJson(JJson::parse(R"({"id":"M"})")).placementCheck);
    return 0;
}
