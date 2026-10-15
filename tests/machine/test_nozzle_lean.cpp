// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// A nozzle's lean (JPMountConfig, calibrated at two heights): a move to a place at a Z puts the axes at the place
// less the offsets at that Z (offset + lean x (Z - the lean's Z)), and where the nozzle is reads its axes plus the
// offsets at its Z now (JPCell::offsetsAt). Without a lean, the offsets at every height; the file keeps the lean.
#undef NDEBUG
#include <cassert>

#include "machine/JPCell.h"

#include <chrono>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <vector>
#include <thread>

using namespace jf;

namespace {

JPCellConfig cellConfig(bool leaning) {
    const std::string json = R"({
      "name": "Test",
      "drivers": [ { "id": "D", "name": "Gantry", "statusIntervalMs": 10, "commandTimeoutMs": 500, "connectWaitMs": 0,
                     "link": { "type": "simulated", "simulator": {
                         "identity": [ "[VER:1.1f.20250101:]", "[FIRMWARE:grblHAL]" ], "axisLetters": [ "X", "Y", "Z" ] } } } ],
      "heads": [ { "id": "H", "name": "Head" } ],
      "axes": [ { "id": "X", "name": "x", "kind": "controller", "type": "x", "driver": "D", "letter": "X",
                  "homeCoordinate": 390, "feedratePerSecond": 100,
                  "softLimits": { "low": 0, "high": 390, "lowEnabled": true, "highEnabled": true } },
                { "id": "Y", "name": "y", "kind": "controller", "type": "y", "driver": "D", "letter": "Y",
                  "homeCoordinate": 444, "feedratePerSecond": 100 },
                { "id": "Z", "name": "z", "kind": "controller", "type": "z", "driver": "D", "letter": "Z",
                  "feedratePerSecond": 50, "safeZone": { "low": 0, "high": 0, "lowEnabled": true, "highEnabled": true } },
                { "id": "ZN", "name": "zn", "kind": "mapped", "type": "z", "inputAxis": "Z",
                  "map": { "input0": 0, "output0": 0, "input1": -1, "output1": -1 } } ],
      "nozzles": [ { "id": "N", "name": "Left", "mount": { "head": "H", "axisX": "X", "axisY": "Y", "axisZ": "ZN",
                                                          "offset": { "x": -20, "y": -62, "z": 0 })" +
                                                          std::string(leaning ? R"(, "lean": { "x": 0.01, "y": -0.02, "z": -10 })" : "")
                                                          + R"( } } ],
      "cameras": [ { "id": "C", "name": "Top", "mount": { "head": "H", "axisX": "X", "axisY": "Y" },
                     "device": { "backend": "simulated", "width": 64, "height": 48, "fps": 10 } } ]
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

struct Latch {
    std::mutex m;
    std::condition_variable cv;
    std::optional<std::pair<bool, std::string>> value;
    void set(bool v, std::string w) { { std::lock_guard lk(m); value = { v, w }; } cv.notify_all(); }
    std::pair<bool, std::string> take() {
        std::unique_lock lk(m);
        const bool got = cv.wait_for(lk, std::chrono::seconds(3), [&] { return value.has_value(); });
        assert(got);
        auto v = *value;
        value.reset();
        return v;
    }
};

bool near(double a, double b) { return std::abs(a - b) < 1e-6; }

} // namespace

int main() {
    for (const bool leaning : { true, false }) {
        const JPCellConfig config = cellConfig(leaning);
        const JPMountConfig& m = config.nozzles[0].mount;
        assert(m.leans() == leaning);
        if (leaning) {
            const JPMountConfig back = JPMountConfig::fromJson(m.toJson());
            assert(back.leanX == 0.01 && back.leanY == -0.02 && back.leanZ == -10);
        }
        JPCell cell(config, profiles());
        Latch connected, motion;
        cell.onConnection.connect([&](bool ok, std::string w) { connected.set(ok, w); });
        cell.onMotion.connect([&](bool ok, std::string w) { motion.set(ok, w); });
        cell.connect();
        assert(connected.take().first);
        cell.home();
        assert(motion.take().first);
        std::string why;
        // Down to Z -15, 5 mm below the lean's Z: the tip 0.05 mm less in X and 0.1 mm more in Y than at -10.
        assert(cell.moveToolAndWait(m, { 100.0, 200.0, -15.0, std::nullopt }, 1.0, why));
        const auto p = cell.jogBase();
        const double ox = leaning ? -20 + 0.01 * -5 : -20, oy = leaning ? -62 + -0.02 * -5 : -62;
        assert(near(p.at("X"), 100 - ox) && near(p.at("Y"), 200 - oy) && near(p.at("Z"), -15));
        const auto [hx, hy] = cell.offsetsAt(m);   // at its Z now
        assert(near(hx, ox) && near(hy, oy));
        const auto [ux, uy] = cell.offsetsAt(m, -10.0);
        assert(near(ux, -20) && near(uy, -62));
        cell.disconnect();
    }
    return 0;
}
