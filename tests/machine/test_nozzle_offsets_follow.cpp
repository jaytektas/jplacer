// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// OpenPnP's adjustHeadOffsetsDependencies: a nozzle's X, Y offsets changed (by hand, or by Calibrate Precise
// Offsets) take along what depends on them. Every tip's runout forgotten (it was measured against the old offsets);
// the nozzle's manual tip change location moved by the same; an actuator fastened to it (the same X, Y offsets it
// had) given the new ones, and nothing else; for the head's first nozzle, the camera looking up moved by the same,
// but not for another nozzle. A Z change alone keeps the runout; offsets that were not set (all zero) move nothing
// else; a change of no more than 0.01 mm keeps the runout.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "machine/JPCellConfig.h"

#include <cmath>

using namespace jf;

namespace {

bool near(double a, double b) { return std::abs(a - b) < 1e-9; }

JPCellConfig cell() {
    JPCellConfig c;
    JPHeadConfig h;
    h.id = "H";
    c.heads.push_back(h);
    for (const auto& [id, x, y] : { std::tuple { "L", -19.925, -61.945 }, std::tuple { "R", 20.0, -61.9 } }) {
        JPNozzleConfig n;
        n.id = id;
        n.name = id;
        n.mount.headId = "H";
        n.mount.offsetX = x;
        n.mount.offsetY = y;
        c.nozzles.push_back(n);
    }
    c.nozzles[0].manualChangeLocation = JPMachineLocation { 100, 50, -5, 0 };
    JPActuatorConfig pushPull;
    pushPull.id = "PP";
    pushPull.mount.headId = "H";
    pushPull.mount.offsetX = -19.925;
    pushPull.mount.offsetY = -61.945;
    pushPull.mount.offsetZ = 3;
    c.actuators.push_back(pushPull);
    JPActuatorConfig other = pushPull;
    other.id = "Other";
    other.mount.offsetX = 5;
    c.actuators.push_back(other);
    JPCameraConfig top;
    top.id = "Top";
    top.mount.headId = "H";
    c.cameras.push_back(top);
    JPCameraConfig up;
    up.id = "Up";
    up.looksUp = true;
    up.mount.offsetX = 65.885;
    up.mount.offsetY = 97.575;
    up.mount.offsetZ = -24;
    c.cameras.push_back(up);
    JPNozzleTipConfig t;
    t.id = "T";
    t.runout["L"] = JPRunout {};
    t.runout["R"] = JPRunout {};
    c.nozzleTips.push_back(t);
    return c;
}

} // namespace

int main() {
    // The default (first) nozzle moved 0.6, -0.6.
    {
        const JPCellConfig before = cell();
        JPCellConfig c = before;
        c.nozzles[0].mount.offsetX += 0.6;
        c.nozzles[0].mount.offsetY -= 0.6;
        c.followNozzleOffsets(before);
        assert(c.nozzleTips[0].runout.empty());
        assert(near(c.nozzles[0].manualChangeLocation->x, 100.6) && near(c.nozzles[0].manualChangeLocation->y, 49.4));
        assert(near(c.actuators[0].mount.offsetX, -19.325) && near(c.actuators[0].mount.offsetY, -62.545) && c.actuators[0].mount.offsetZ == 3);
        assert(c.actuators[1].mount.offsetX == 5);                    // not fastened to it
        assert(c.cameras[0].mount.offsetX == 0 && c.cameras[0].mount.offsetY == 0);   // the head camera stays
        assert(near(c.cameras[1].mount.offsetX, 66.485) && near(c.cameras[1].mount.offsetY, 96.975));
        assert(c.nozzles[1].mount.offsetX == 20.0);
    }
    // Another nozzle: the camera looking up stays.
    {
        const JPCellConfig before = cell();
        JPCellConfig c = before;
        c.nozzles[1].mount.offsetX += 0.3;
        c.followNozzleOffsets(before);
        assert(c.nozzleTips[0].runout.empty() && c.cameras[1].mount.offsetX == 65.885);
    }
    // Z alone, or no more than 0.01 mm: the runout kept.
    {
        const JPCellConfig before = cell();
        JPCellConfig c = before;
        c.nozzles[0].mount.offsetZ = 1;
        c.followNozzleOffsets(before);
        assert(c.nozzleTips[0].runout.size() == 2);
        c = before;
        c.nozzles[0].mount.offsetX += 0.005;
        c.followNozzleOffsets(before);
        assert(c.nozzleTips[0].runout.size() == 2);
    }
    // Offsets not set (all zero): nothing else was set from them.
    {
        JPCellConfig before = cell();
        before.nozzles[0].mount.offsetX = before.nozzles[0].mount.offsetY = 0;
        JPCellConfig c = before;
        c.nozzles[0].mount.offsetX = 1;
        c.followNozzleOffsets(before);
        assert(c.cameras[0].mount.offsetX == 0 && c.cameras[1].mount.offsetX == 65.885 && c.nozzleTips[0].runout.empty());
    }
    return 0;
}
