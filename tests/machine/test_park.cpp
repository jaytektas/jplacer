// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// Parking the head: a nozzle down on a Z shared by two nozzles (a seesaw,
// safe only level) comes up into the safe zone first, then the head goes to
// its park place by its camera (an offset nozzle beside it does not count),
// as near as the soft limits allow where the place is past them.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "machine/JPCell.h"

#include <chrono>
#include <cmath>
#include <condition_variable>
#include <mutex>

using namespace jf;

namespace {

JPCellConfig cellConfig(bool withPark) {
    const std::string json = R"({
      "name": "Test",
      "drivers": [ { "id": "D", "name": "Gantry", "statusIntervalMs": 10, "commandTimeoutMs": 500, "connectWaitMs": 0,
                     "link": { "type": "simulated", "simulator": {
                         "identity": [ "[VER:1.1f.20250101:]", "[FIRMWARE:grblHAL]" ], "axisLetters": [ "X", "Y", "Z" ] } } } ],
      "heads": [ { "id": "H", "name": "Head")" + std::string(withPark ? R"(, "park": { "x": 395, "y": 420 })" : "") + R"( } ],
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
                                                          "offset": { "x": -20, "y": -62, "z": 0 } } } ],
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
    {
        JPCell cell(cellConfig(true), profiles());
        Latch connected, motion;
        cell.onConnection.connect([&](bool ok, std::string w) { connected.set(ok, w); });
        cell.onMotion.connect([&](bool ok, std::string w) { motion.set(ok, w); });
        cell.connect();
        assert(connected.take().first);
        cell.home();
        assert(motion.take().first);

        // The left nozzle down 5 mm: its shared Z out of the safe zone.
        std::string why;
        assert(cell.moveAxesAndWait({ { "X", 200 }, { "Y", 100 }, { "ZN", -5 } }, 1.0, why));
        assert(near(cell.jogBase().at("Z"), -5));   // this nozzle follows Z the same way (the other would go up)
        motion.take();
        cell.park("H", 1.0);
        const auto [ok, parkWhy] = motion.take();
        assert(ok && parkWhy.empty());
        const auto p = cell.jogBase();
        // By the camera, not the nozzle; X 395 is past its soft limit, so as near as it allows.
        assert(near(p.at("Z"), 0) && near(p.at("X"), 390) && near(p.at("Y"), 420));
        cell.disconnect();
    }
    {
        // No park place: refused, and nothing moves.
        JPCell cell(cellConfig(false), profiles());
        Latch connected, motion;
        cell.onConnection.connect([&](bool ok, std::string w) { connected.set(ok, w); });
        cell.onMotion.connect([&](bool ok, std::string w) { motion.set(ok, w); });
        cell.connect();
        assert(connected.take().first);
        cell.home();
        assert(motion.take().first);
        cell.park("H", 1.0);
        const auto [ok, why] = motion.take();
        assert(!ok && why.find("park place") != std::string::npos);
        cell.disconnect();
    }
    {
        // Z park, as OpenPnP's: the tool's Z to the low end of its safe zone, from below it or from inside it.
        JPCellConfig config = cellConfig(true);
        for (JPAxisConfig& a : config.axes)
            if (a.id == "Z") a.safeZoneLow = -2;
        JPCell cell(config, profiles());
        Latch connected, motion;
        cell.onConnection.connect([&](bool ok, std::string w) { connected.set(ok, w); });
        cell.onMotion.connect([&](bool ok, std::string w) { motion.set(ok, w); });
        cell.connect();
        assert(connected.take().first);
        cell.home();
        assert(motion.take().first);
        std::string why;
        for (const double from : { -5.0, -1.0 }) {
            assert(cell.moveAxesAndWait({ { "ZN", from } }, 1.0, why));
            motion.take();
            cell.parkZ(config.nozzles[0].mount, 1.0);
            assert(motion.take().first);
            assert(near(cell.jogBase().at("Z"), -2));
        }
        cell.disconnect();
    }
    {
        // Dynamic Safe Z: a nozzle on its own Z carrying a part 3 mm tall goes
        // 3 mm higher, its part's bottom at the zone's low end (within the zone);
        // without a part, to the low end itself.
        JPCellConfig config = cellConfig(true);
        for (JPAxisConfig& a : config.axes)
            if (a.id == "Z") {
                a.safeZoneLow = -2;
                a.safeZoneHigh = 2;
            }
        config.nozzles[0].mount.axisZ = "Z";
        config.nozzles[0].dynamicSafeZ = true;
        JPCell cell(config, profiles());
        Latch connected, motion;
        cell.onConnection.connect([&](bool ok, std::string w) { connected.set(ok, w); });
        cell.onMotion.connect([&](bool ok, std::string w) { motion.set(ok, w); });
        cell.connect();
        assert(connected.take().first);
        cell.home();
        assert(motion.take().first);
        std::string why;
        assert(cell.moveAxesAndWait({ { "Z", -5 } }, 1.0, why));
        motion.take();
        cell.setPartHeight("N", 3);
        cell.parkZ(config.nozzles[0].mount, 1.0);
        assert(motion.take().first && near(cell.jogBase().at("Z"), 1));
        // Head Safe Z too; and a taller part only as high as the zone goes.
        assert(cell.moveAxesAndWait({ { "Z", -5 } }, 1.0, why));
        motion.take();
        cell.setPartHeight("N", 10);
        cell.safeZ("H", 1.0);
        assert(motion.take().first && near(cell.jogBase().at("Z"), 2));
        cell.setPartHeight("N", 0);
        cell.parkZ(config.nozzles[0].mount, 1.0);
        assert(motion.take().first && near(cell.jogBase().at("Z"), -2));
        cell.disconnect();
    }
    {
        // Unsafe Z Roaming: a nozzle left 5 mm below its safe zone stays there
        // jogged 5 mm, and goes up with the jog that takes it past 10 mm away.
        JPCell cell(cellConfig(true), profiles());
        Latch connected, motion;
        cell.onConnection.connect([&](bool ok, std::string w) { connected.set(ok, w); });
        cell.onMotion.connect([&](bool ok, std::string w) { motion.set(ok, w); });
        cell.connect();
        assert(connected.take().first);
        cell.home();
        assert(motion.take().first);
        std::string why;
        assert(cell.moveAxesAndWait({ { "X", 200 }, { "Y", 100 }, { "ZN", -5 } }, 1.0, why));
        motion.take();
        cell.jog("N", 5, 0, 0, 0, 1.0);
        assert(motion.take().first && near(cell.jogBase().at("ZN"), -5));
        cell.jog("N", 6, 0, 0, 0, 1.0);
        assert(motion.take().first && near(cell.jogBase().at("ZN"), 0) && near(cell.jogBase().at("X"), 211));
        cell.disconnect();
    }
    return 0;
}
