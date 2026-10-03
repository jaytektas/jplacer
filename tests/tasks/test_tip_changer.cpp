// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// A nozzle tip's changer steps run on a simulated machine: a place is where
// the nozzle goes (its offset taken off), the first move comes in from safe
// Z, the list ends at safe Z, and asking before each step stops before
// anything moves.
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
      "axes": [ { "id": "X", "name": "x", "kind": "controller", "type": "x", "driver": "D", "letter": "X", "feedratePerSecond": 100 },
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

    // Every step asked about: no to the first, and nothing moves.
    std::vector<std::string> asked;
    JPTipChanger::Hooks no{ [&](const std::string& q) { asked.push_back(q); return false; }, nullptr };
    std::string why;
    const double x0 = cell.jogBase().at("X");
    assert(!JPTipChanger::run(cell, cell.config(), nozzle, steps, "Loading T", true, no, why));
    assert(asked.size() == 1 && asked[0].find("step 1 of 3: move to X 50 Z -5") != std::string::npos);
    assert(why.find("stopped before step 1") == 0 && cell.jogBase().at("X") == x0);

    // Run: the Z the controller went through, and where it ended.
    std::mutex m;
    std::vector<std::string> sent;
    auto watch = cell.onTraffic.connect([&](std::string, bool out, std::string line) {
        std::lock_guard lk(m);
        if (out && line.rfind("G1 ", 0) == 0) sent.push_back(line);
    });
    JPTipChanger::Hooks yes{ [](const std::string&) { return true; }, nullptr };
    assert(JPTipChanger::run(cell, cell.config(), nozzle, steps, "Loading T", true, yes, why));
    watch();
    // In from safe Z: across first (Z untouched), then down; along; up at the end.
    std::lock_guard lk(m);
    assert(sent.size() >= 4);
    assert(sent[0].find("X40") != std::string::npos && sent[0].find("Z") == std::string::npos);
    assert(sent[1].find("Z-5") != std::string::npos && sent[1].find("X") == std::string::npos);
    assert(sent[2].find("X50") != std::string::npos);
    assert(sent.back().find("Z0") != std::string::npos);
    assert(until([&] { return cell.positions().at("X") == 50 && cell.positions().at("Z") == 0; }));

    std::puts("test_tip_changer: ok");
    return 0;
}
