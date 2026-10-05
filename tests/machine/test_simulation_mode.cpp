// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// OpenPnP's Simulation Mode, Replace Drivers?: a cell set up for a real
// controller on a serial port runs on a simulated one instead (its axes'
// letters kept), connects, homes and moves; the cell file keeps the real
// link. Off, it is the serial port again (which is not there).
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "machine/JPCell.h"

#include <chrono>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <optional>

using namespace jf;

namespace {

JPCellConfig cellConfig(const char* mode) {
    const std::string json = R"({
      "name": "Bench",
      "drivers": [ { "id": "D", "name": "Gantry", "statusIntervalMs": 10, "commandTimeoutMs": 500, "connectWaitMs": 0,
                     "link": { "type": "serial", "port": "/dev/jplacer-no-such-port", "baud": 115200 } } ],
      "heads": [ { "id": "H", "name": "Head" } ],
      "axes": [ { "id": "X", "name": "x", "kind": "controller", "type": "x", "driver": "D", "letter": "X", "feedratePerSecond": 100 },
                { "id": "Y", "name": "y", "kind": "controller", "type": "y", "driver": "D", "letter": "Y", "feedratePerSecond": 100 } ],
      "nozzles": [ { "id": "N", "name": "N1", "mount": { "head": "H", "axisX": "X", "axisY": "Y" } } ],
      "simulation": { "mode": ")" + std::string(mode) + R"(", "replaceDrivers": true, "homingError": { "x": 0.2, "y": -0.1 } }
    })";
    JPCellConfig c;
    std::string error;
    const bool ok = c.fromJson(JJson::parse(json), error);
    assert(ok);
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

} // namespace

int main() {
    // Kept and read back.
    {
        const JPCellConfig c = cellConfig("DynamicImperfectionsMachine");
        assert(c.simulation.dynamic() && c.simulation.imperfect() && c.simulation.replacesDrivers());
        assert(c.simulation.homingErrorX == 0.2 && c.simulation.homingErrorY == -0.1);
        JPCellConfig back;
        std::string error;
        assert(back.fromJson(c.toJson(), error) && back.simulation.mode == c.simulation.mode);
        assert(back.drivers.front().link["type"].str() == "serial");   // the real link kept
    }
    // Simulated: connects, homes and moves.
    {
        JPCell cell(cellConfig("IdealMachine"), profiles());
        Latch connected, motion;
        cell.onConnection.connect([&](bool ok, std::string w) { connected.set(ok, w); });
        cell.onMotion.connect([&](bool ok, std::string w) { motion.set(ok, w); });
        cell.connect();
        const auto c = connected.take();
        if (!c.first) std::fprintf(stderr, "why: %s\n", c.second.c_str());
        assert(c.first);
        assert(cell.config().drivers.front().link["type"].str() == "serial");
        cell.home();
        assert(motion.take().first);
        std::string why;
        assert(cell.moveAxesAndWait({ { "X", 12.5 }, { "Y", 3 } }, 1.0, why));
        assert(std::abs(cell.positions().at("X") - 12.5) < 1e-6);
        cell.disconnect();
    }
    // Off: the serial port, which is not there.
    {
        JPCell cell(cellConfig("Off"), profiles());
        Latch connected;
        cell.onConnection.connect([&](bool ok, std::string w) { connected.set(ok, w); });
        cell.connect();
        assert(!connected.take().first);
    }
    return 0;
}
