// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// A cell over a simulated controller: it connects, follows controller axes from
// status reports and a mapped axis through its map, switches and reads an
// actuator, and reports a cell whose controller cannot connect.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "machine/JPCell.h"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>

using namespace jf;

namespace {

std::vector<JPFirmwareProfile> profiles() {
    JPFirmwareProfile p;
    std::string error;
    const bool ok = p.load(std::string(JPLACER_PROFILES_DIR) + "/grblhal.json", error);
    assert(ok);
    return { p };
}

JPCellConfig cellConfig() {
    const std::string json = R"({
      "name": "Test",
      "drivers": [ { "id": "D", "name": "Gantry", "statusIntervalMs": 10, "commandTimeoutMs": 500, "connectWaitMs": 0,
                     "link": { "type": "simulated", "simulator": {
                         "identity": [ "[VER:1.1f.20250101:]", "[FIRMWARE:grblHAL]" ],
                         "axisLetters": [ "X", "Y", "Z" ],
                         "replies": { "M1000 P1": "-31000" } } } } ],
      "heads": [ { "id": "H", "name": "Head" } ],
      "axes": [ { "id": "X",  "name": "x",  "kind": "controller", "type": "x", "driver": "D", "letter": "X",
                  "homeCoordinate": 390, "feedratePerSecond": 100,
                  "softLimits": { "low": 0, "high": 400, "lowEnabled": true, "highEnabled": true } },
                { "id": "Z",  "name": "z",  "kind": "controller", "type": "z", "driver": "D", "letter": "Z",
                  "feedratePerSecond": 50 },
                { "id": "ZR", "name": "zr", "kind": "mapped", "type": "z", "inputAxis": "Z",
                  "map": { "input0": -1, "output0": 1, "input1": 0, "output1": 0 } } ],
      "nozzles": [ { "id": "N", "name": "Right", "mount": { "head": "H", "axisX": "X", "axisZ": "ZR" } } ],
      "actuators": [ { "id": "V", "name": "Vacuum", "driver": "D", "index": "1",
                       "onCommand": "M64 P{index}", "offCommand": "M65 P{index}",
                       "readCommand": "M1000 P{index}", "readPattern": "^(-?\\d+)$" } ]
    })";
    JPCellConfig c;
    std::string error;
    const bool ok = c.fromJson(JJson::parse(json), error);
    assert(ok && c.problems().empty());
    return c;
}

// Waits for a signal's value, set from another thread.
template <class T>
struct Latch {
    std::mutex m;
    std::condition_variable cv;
    std::optional<T> value;
    void set(T v) { { std::lock_guard lk(m); value = std::move(v); } cv.notify_all(); }
    T take() {
        std::unique_lock lk(m);
        const bool got = cv.wait_for(lk, std::chrono::seconds(2), [&] { return value.has_value(); });
        assert(got);
        T v = std::move(*value);
        value.reset();
        return v;
    }
};

} // namespace

int main() {
    {
        JPCell cell(cellConfig(), profiles());
        Latch<std::pair<bool, std::string>> connection;
        Latch<std::pair<bool, std::string>> actuator;
        cell.onConnection.connect([&](bool ok, std::string why) { connection.set({ ok, why }); });
        cell.onActuator.connect([&](std::string, bool ok, std::string v) { actuator.set({ ok, v }); });

        cell.connect();
        const auto [ok, why] = connection.take();
        assert(ok && why.empty() && cell.isConnected());
        assert(cell.firmware().at("D") == "grblHAL");

        cell.sendLine("D", "G0 X12 Z-2");
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (cell.positions()["ZR"] != 2.0 && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        const auto pos = cell.positions();
        assert(pos.at("X") == 12.0 && pos.at("Z") == -2.0 && pos.at("ZR") == 2.0);

        cell.switchActuator("V", true);
        assert(actuator.take() == std::make_pair(true, std::string("on")));
        cell.readActuator("V");
        assert(actuator.take() == std::make_pair(true, std::string("-31000")));

        // Motion: refused until homed, then a tool jogs along its own axes.
        Latch<std::pair<bool, std::string>> motion;
        cell.onMotion.connect([&](bool ok, std::string why) { motion.set({ ok, why }); });
        cell.jog("N", 1, 0, 0, 0, 0.5);
        const auto [unhomedOk, unhomedWhy] = motion.take();
        assert(!unhomedOk && unhomedWhy.find("not homed") != std::string::npos);

        cell.home();
        assert(motion.take().first && cell.isHomed());
        auto settle = [&](const char* axis, double want) {
            const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            while (cell.positions()[axis] != want && std::chrono::steady_clock::now() < until)
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            return cell.positions()[axis] == want;
        };
        assert(settle("X", 390.0));                        // G92 made the home coordinate true

        cell.jog("N", 5, 0, 0, 0, 0.5);
        assert(motion.take().first && settle("X", 395.0));
        cell.jog("N", 0, 0, 1.5, 0, 1.0);                  // the nozzle's Z is mapped: its input goes the other way
        assert(motion.take().first && settle("ZR", 1.5) && settle("Z", -1.5));

        // A step from where the axis was sent, not from where it reports: back
        // to 390 exactly, the limit itself, even when the report is 389.999.
        cell.sendLine("D", "G92 X395.001");                // the report now reads a hair high
        assert(settle("X", 395.001));
        cell.jog("N", -5, 0, 0, 0, 0.5);
        assert(motion.take().first && settle("X", 390.0));
        cell.jog("N", 5, 0, 0, 0, 0.5);
        assert(motion.take().first && settle("X", 395.0));

        cell.jog("N", 10, 0, 0, 0, 0.5);                   // 405 is past the soft limit
        const auto [limitOk, limitWhy] = motion.take();
        assert(!limitOk && limitWhy.find("soft limits") != std::string::npos && settle("X", 395.0));

        cell.disconnect();
        assert(!connection.take().first && !cell.isHomed());
    }
    {
        // A controller on a link that does not exist: the cell says why.
        JPCellConfig c = cellConfig();
        c.drivers[0].link = JJson::object();
        c.drivers[0].link["type"] = "carrier-pigeon";
        JPCell cell(c, profiles());
        Latch<std::pair<bool, std::string>> connection;
        cell.onConnection.connect([&](bool ok, std::string why) { connection.set({ ok, why }); });
        cell.connect();
        const auto [ok, why] = connection.take();
        assert(!ok && why.find("carrier-pigeon") != std::string::npos && !cell.isConnected());
    }
    return 0;
}
