// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// A nozzle's placement check (JPPlacementCheck): placements made of a known offset error at the place height, a
// part turning with the angle, bottom vision's bias, a drift and the test object's own offset (its top's centre
// against its underneath's, turning as the object is turned in all; on bench a dot printed off-centre) give them back. Where the object truly sits on the
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
    const double qx = -0.072, qy = 0.241;       // the test object's own offset, top against underneath (bench's)
    const double sx = 0.07, sy = -0.12;         // where the nozzle picks it, off its true centre (its offset error)
    auto turn = [](double deg, double x, double y) {
        const double a = deg * M_PI / 180;
        return std::pair { x * std::cos(a) - y * std::sin(a), x * std::sin(a) + y * std::cos(a) };
    };
    JPPlacementCheck c;
    c.when = "2026-10-15 15:30";
    c.z = -23.8;
    c.drifts = { { dx, dy }, { dx, dy }, { dx, dy } };
    double turned = 0;
    for (int round = 0; round < 2; ++round)
        for (const double angle : { 0.0, 90.0, 180.0, 270.0 }) {
            // Picked where the head camera found it: its true centre the object's offset (turned as it is) away.
            const double held = turned;
            const auto [px, py] = turn(held, qx, qy);
            const auto [tx, ty] = turn(angle, sx - px, sy - py);   // where it truly sits on the nozzle, turned
            turned = std::fmod(turned + angle, 360.0);
            const auto [vtx, vty] = turn(angle, vx, vy);
            const auto [qtx, qty] = turn(turned, qx, qy);
            // Bottom vision sees where it sits plus its bias; it lands at the nozzle's error, the turning part, less
            // vision's bias, plus the drift; the head camera finds it the object's offset off.
            c.places.push_back({ angle, tx + bx, ty + by, ex + vtx - bx + dx + qtx, ey + vty - by + dy + qty, held, turned });
        }
    double ox = 0, oy = 0;
    assert(c.objectOffset(ox, oy) && near(ox, qx) && near(oy, qy));
    assert(near(c.biasX(), ex - bx + dx) && near(c.biasY(), ey - by + dy));
    assert(near(c.driftX(), dx) && near(c.driftY(), dy));
    assert(near(c.visionBiasX(), bx) && near(c.visionBiasY(), by));
    assert(near(c.offsetErrorX(), ex) && near(c.offsetErrorY(), ey));
    double tx = 0, ty = 0;
    c.turning(tx, ty);
    assert(near(tx, vx) && near(ty, vy));
    assert(c.worstResidual() < 1e-9);
    // Placements that did not turn the object apart from their angles: its offset cannot be told, and is none.
    JPPlacementCheck same = c;
    for (JPPlacementCheck::Place& p : same.places) p.turned = p.angle;
    assert(!same.objectOffset(ox, oy) && ox == 0 && oy == 0);

    JPNozzleConfig n;
    n.id = "N";
    n.placementCheck = c;
    const JPNozzleConfig back = JPNozzleConfig::fromJson(n.toJson());
    assert(back.placementCheck && back.placementCheck->when == c.when && back.placementCheck->z == -23.8);
    assert(back.placementCheck->drifts.size() == 3 && back.placementCheck->places.size() == 8);
    assert(near(back.placementCheck->offsetErrorY(), ey) && near(back.placementCheck->places[5].angle, 90));
    assert(near(back.placementCheck->places[2].heldTurn, 90) && near(back.placementCheck->places[2].turned, 270));
    assert(!JPNozzleConfig::fromJson(JJson::parse(R"({"id":"M"})")).placementCheck);
    return 0;
}
