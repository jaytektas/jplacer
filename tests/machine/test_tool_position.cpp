// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// Where a tool is taken to be (JPCell::offsetsNow): a nozzle sent to a place, its axes carrying its tip's runout
// at the angle it went to, is read back at that place, not off by the runout; and, its lean taken in, where it
// comes down to at a height, wherever it hangs now. Without the runout, a camera sent to the nozzle and the
// nozzle sent back to the camera moved the head by it each time; without the height, a camera sent to a nozzle
// up at safe Z after a place landed beside the part by the lean over that height. Jogged up or down, a leaning
// nozzle's tip stays over the same place.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "machine/JPCell.h"

#include <chrono>
#include <cmath>
#include <thread>

using namespace jf;

namespace {

JPCellConfig cellConfig() {
    const std::string json = R"({
      "name": "Head",
      "drivers": [ { "id": "D", "name": "Gantry", "statusIntervalMs": 10, "commandTimeoutMs": 500, "connectWaitMs": 0,
                     "link": { "type": "simulated", "simulator": {
                         "identity": [ "[VER:1.1f.20250101:]", "[FIRMWARE:grblHAL]" ], "axisLetters": [ "X", "Y", "Z", "A" ] } } } ],
      "heads": [ { "id": "H", "name": "Head" } ],
      "axes": [ { "id": "X", "name": "x", "kind": "controller", "type": "x", "driver": "D", "letter": "X", "feedratePerSecond": 100 },
                { "id": "Y", "name": "y", "kind": "controller", "type": "y", "driver": "D", "letter": "Y", "feedratePerSecond": 100 },
                { "id": "Z", "name": "z", "kind": "controller", "type": "z", "driver": "D", "letter": "Z", "feedratePerSecond": 50,
                  "safeZone": { "low": 0, "high": 0, "lowEnabled": true, "highEnabled": false } },
                { "id": "C", "name": "c", "kind": "controller", "type": "rotation", "driver": "D", "letter": "A",
                  "feedratePerSecond": 360 } ],
      "nozzleTips": [ { "id": "T", "name": "Tip" } ],
      "nozzles": [ { "id": "N", "name": "N1", "tip": "T", "tips": [ "T" ],
                     "mount": { "head": "H", "axisX": "X", "axisY": "Y", "axisZ": "Z", "axisRotation": "C",
                                "offset": { "x": -20, "y": -60, "z": 0 } } } ]
    })";
    JPCellConfig c;
    std::string error;
    const bool ok = c.fromJson(JJson::parse(json), error);
    assert(ok && c.problems().empty());
    return c;
}

std::vector<JPFirmwareProfile> profiles() {
    JPFirmwareProfile p;
    std::string error;
    const bool ok = p.load(std::string(JPLACER_PROFILES_DIR) + "/grblhal.json", error);
    assert(ok);
    return { p };
}

bool near(double a, double b) { return std::abs(a - b) < 1e-6; }

} // namespace

int main() {
    JPCellConfig config = cellConfig();
    // The tip swings 0.2 mm round its axis (the model, no centre offset); the nozzle leans 0.04 mm in X per mm of Z
    // from Z -24, where its offsets hold.
    JPNozzleTipConfig& tip = config.nozzleTips[0];
    tip.runoutCalibration.enabled = true;
    JPRunout r;
    r.algorithm = "ModelNoOffset";
    r.radius = 0.2;
    r.phaseDeg = 30;
    tip.runout["N"] = r;
    JPMountConfig& m = config.nozzles[0].mount;
    m.leanX = 0.04;
    m.leanZ = -24;
    JPCell cell(config, profiles());
    cell.connect();
    for (int i = 0; i < 300 && !cell.isConnected(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    cell.home();
    for (int i = 0; i < 300 && !cell.isHomed(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    assert(cell.isHomed());

    const JPMountConfig& mount = cell.config().nozzles[0].mount;
    auto at = [&](std::optional<double> z) {
        const auto axes = cell.jogBase();
        const auto [ox, oy] = cell.offsetsNow(mount, z);
        return std::pair { axes.at("X") + ox, axes.at("Y") + oy };
    };
    std::string why;
    // Down at a place, turned 90: read back there (the axes carry the swing at 90).
    assert(cell.moveToolAndWait(mount, { 50.0, 40.0, -24.0, 90.0 }, 1.0, why));
    auto [x, y] = at(std::nullopt);
    assert(near(x, 50) && near(y, 40));
    double sx = 0, sy = 0;
    r.offset(90, sx, sy);
    assert(std::hypot(sx, sy) > 0.1);   // the runout the old reading was off by
    // Up at safe Z: hanging 0.96 mm further in X by its lean, but where it comes down to at -24 is the place still.
    assert(cell.moveAxesAndWait({ { "Z", 0.0 } }, 1.0, why));
    std::tie(x, y) = at(std::nullopt);
    assert(near(x, 50 + 0.04 * 24) && near(y, 40));
    std::tie(x, y) = at(-24.0);
    assert(near(x, 50) && near(y, 40));
    // Jogged down 10: the tip held over the same place (X moved the other way by its lean), as a turn holds it.
    const auto before = at(std::nullopt);
    cell.jog("N", 0, 0, -10, 0, 1.0);
    for (int i = 0; i < 300 && !near(cell.jogBase().at("Z"), -10); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    for (int i = 0; i < 100 && cell.isMoving(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    const auto after = at(std::nullopt);
    assert(near(cell.jogBase().at("Z"), -10) && near(after.first, before.first) && near(after.second, before.second));
    cell.disconnect();
    return 0;
}
