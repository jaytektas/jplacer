// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// The changer's moves without backlash compensation's extra moves (OpenPnP's SpeedOverPrecision): X compensated
// one-sided, 1.5 mm, goes past its target and back on an ordinary move, but among the changer's slots each step
// goes straight to its place (past it, the tip would hit the slot's wall); compensation is back after.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "tasks/JPTipChanger.h"

#include <chrono>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace jf;

namespace {

JPCellConfig cellConfig() {
    const std::string json = R"({
      "name": "Changer",
      "drivers": [ { "id": "D", "name": "Gantry", "statusIntervalMs": 10, "commandTimeoutMs": 500, "connectWaitMs": 0,
                     "link": { "type": "simulated", "simulator": {
                         "identity": [ "[VER:1.1f.20250101:]", "[FIRMWARE:grblHAL]" ],
                         "axisLetters": [ "X", "Y", "Z" ] } } } ],
      "heads": [ { "id": "H", "name": "Head" } ],
      "axes": [ { "id": "X", "name": "x", "kind": "controller", "type": "x", "driver": "D", "letter": "X", "feedratePerSecond": 100,
                  "backlash": "oneSided", "backlashOffset": 1.5, "backlashSpeedFactor": 0.5 },
                { "id": "Z", "name": "z", "kind": "controller", "type": "z", "driver": "D", "letter": "Z", "feedratePerSecond": 50,
                  "safeZone": { "low": 0, "high": 0, "lowEnabled": true, "highEnabled": true } } ],
      "nozzles": [ { "id": "N", "name": "Left", "mount": { "head": "H", "axisX": "X", "axisZ": "Z", "offset": { "x": 10, "y": 0, "z": 0 } } } ],
      "actuators": [ { "id": "V", "name": "Latch", "driver": "D", "index": "1", "onCommand": "M64 P{index}", "offCommand": "M65 P{index}" } ]
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

// Waits for `cell` to say `want`.
template <class F>
bool until(F want) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!want() && std::chrono::steady_clock::now() < end) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    return want();
}

} // namespace

int main() {
    JPCell cell(cellConfig(), profiles());
    cell.connect();
    assert(until([&] { return cell.isConnected(); }));
    cell.home();
    assert(until([&] { return cell.isHomed(); }));

    // Loading: in at X 50 and down to Z -5 (the nozzle; X's axis at 40),
    // the latch, along to X 60, then out.
    using K = JPChangerStep::Kind;
    std::vector<JPChangerStep> steps(3);
    steps[0].x = 50;
    steps[0].z = -5;
    steps[1].kind = K::Actuator;
    steps[1].actuatorId = "V";
    steps[2].x = 60;
    const JPNozzleConfig nozzle = cell.config().nozzles[0];

    std::mutex m;
    std::vector<std::string> sent;
    auto watch = cell.onTraffic.connect([&](std::string, bool out, std::string line) {
        std::lock_guard lk(m);
        if (out && line.rfind("G1 ", 0) == 0) sent.push_back(line);
    });
    auto sentHas = [&](const std::string& word) {
        std::lock_guard lk(m);
        for (const std::string& l : sent)
            if (l.find(word) != std::string::npos) return true;
        return false;
    };
    std::string why;

    // An ordinary move: past the target by the offset, then in.
    assert(cell.moveAxesAndWait({ { "X", 20 } }, 1.0, why));
    assert(sentHas("X21.5") && sentHas("X20"));

    // The changer: straight to each place, never past it.
    {
        std::lock_guard lk(m);
        sent.clear();
    }
    JPTipChanger::Hooks yes{ [](const std::string&) { return true; }, nullptr };
    assert(JPTipChanger::run(cell, cell.config(), nozzle, steps, "Loading T", false, yes, why));
    assert(sentHas("X40") && sentHas("X50"));
    assert(!sentHas("X41.5") && !sentHas("X51.5"));

    // After: compensation as before.
    {
        std::lock_guard lk(m);
        sent.clear();
    }
    assert(cell.moveAxesAndWait({ { "X", 30 } }, 1.0, why));
    assert(sentHas("X31.5") && sentHas("X30"));
    watch();

    std::puts("test_tip_changer_backlash: ok");
    return 0;
}
