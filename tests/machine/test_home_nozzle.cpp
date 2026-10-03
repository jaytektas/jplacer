// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// Homing one nozzle's Z on a simulated siamese head (two nozzles on one Z
// motor): the head goes to its park place first, the nozzle's own home
// command runs, and the motor's axis is then at its home coordinate. Homing
// either nozzle homes both; a nozzle with no home command is refused.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "machine/JPCell.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

using namespace jf;

namespace {

JPCellConfig cellConfig() {
    const std::string json = R"({
      "name": "Siamese",
      "drivers": [ { "id": "D", "name": "Gantry", "statusIntervalMs": 10, "commandTimeoutMs": 1000, "connectWaitMs": 0,
                     "link": { "type": "simulated", "simulator": {
                         "identity": [ "[VER:1.1f.20250101:]", "[FIRMWARE:grblHAL]" ],
                         "axisLetters": [ "X", "Y", "Z" ] } } } ],
      "heads": [ { "id": "H", "name": "Head", "park": { "x": 20, "y": 30, "z": 0, "rotation": 0 } } ],
      "axes": [ { "id": "X", "name": "x", "kind": "controller", "type": "x", "driver": "D", "letter": "X", "feedratePerSecond": 100 },
                { "id": "Y", "name": "y", "kind": "controller", "type": "y", "driver": "D", "letter": "Y", "feedratePerSecond": 100 },
                { "id": "Z", "name": "z", "kind": "controller", "type": "z", "driver": "D", "letter": "Z", "feedratePerSecond": 50,
                  "homeCoordinate": 2,
                  "safeZone": { "low": -1, "high": 3, "lowEnabled": true, "highEnabled": true } },
                { "id": "ZL", "name": "zl", "kind": "mapped", "type": "z", "inputAxis": "Z" },
                { "id": "ZR", "name": "zr", "kind": "mapped", "type": "z", "inputAxis": "Z",
                  "map": { "input0": 0, "output0": 0, "input1": 1, "output1": -1 } } ],
      "nozzles": [ { "id": "L", "name": "Left",  "mount": { "head": "H", "axisX": "X", "axisY": "Y", "axisZ": "ZL" },
                     "homeCommand": "M18 Z ; motor off\nG4 P1\nM17 Z\n$HZ\nG92 Z-25.5 ; balance\nG0 Z0" },
                   { "id": "R", "name": "Right", "mount": { "head": "H", "axisX": "X", "axisY": "Y", "axisZ": "ZR" } } ]
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

template <class F>
bool until(F want) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!want() && std::chrono::steady_clock::now() < end) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    return want();
}

struct Motion {
    std::mutex m;
    std::optional<std::pair<bool, std::string>> last;
};

} // namespace

int main() {
    JPCell cell(cellConfig(), profiles());
    Motion motion;
    cell.onMotion.connect([&](bool ok, std::string why) {
        std::lock_guard lk(motion.m);
        motion.last = { ok, why };
    });
    auto take = [&] {
        assert(until([&] { std::lock_guard lk(motion.m); return motion.last.has_value(); }));
        std::lock_guard lk(motion.m);
        auto r = *motion.last;
        motion.last.reset();
        return r;
    };

    // One motor: homing either nozzle homes both.
    assert((cell.nozzlesHomedWith("L") == std::vector<std::string>{ "L", "R" }));
    assert((cell.nozzlesHomedWith("R") == std::vector<std::string>{ "R", "L" }));

    // Refused before the machine is homed.
    cell.connect();
    assert(until([&] { return cell.isConnected(); }));
    cell.homeNozzle("L", 1.0);
    assert(!take().first);

    cell.home();
    assert(until([&] { return cell.isHomed(); }));
    take();

    // Away from the park place, Z down a little.
    cell.moveAxes({ { "X", 5.0 }, { "Y", 6.0 }, { "Z", 1.0 } }, 1.0);
    assert(take().first);

    cell.homeNozzle("L", 1.0);
    const auto [ok, why] = take();
    if (!ok) std::fprintf(stderr, "homeNozzle: %s\n", why.c_str());
    assert(ok);
    const auto at = cell.positions();
    assert(std::abs(at.at("X") - 20) < 1e-3 && std::abs(at.at("Y") - 30) < 1e-3);   // parked
    assert(std::abs(at.at("Z") - 2) < 1e-3);                                         // its home coordinate
    assert(cell.isHomed());

    // A nozzle with no home command: refused, nothing moved.
    cell.homeNozzle("R", 1.0);
    const auto [ok2, why2] = take();
    assert(!ok2 && why2.find("no Z home command") != std::string::npos);

    std::puts("test_home_nozzle: ok");
    return 0;
}
