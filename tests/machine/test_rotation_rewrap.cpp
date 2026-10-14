// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// A rotation that wraps and is limited to -180..180, with continuous motion: a turn the short way past 180 (to 270)
// is followed by the controller being told it is at -90 (G92); the next move under way starts from -90, so a turn of
// a degree more is a degree (bench, 2026-10-14: it started from 270 as sent, and a bottom vision pass's correction of
// 1.2 degrees went the long way round, 361 degrees, slowly).
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "machine/JPCell.h"
#include "machine/JPFirmwareProfile.h"

#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

using namespace jf;

int main() {
    JPFirmwareProfile profile;
    std::string error;
    assert(profile.load(std::string(JPLACER_PROFILES_DIR) + "/grblhal.json", error));
    const std::string json = R"({
      "name": "Test",
      "drivers": [ { "id": "D", "name": "Gantry", "statusIntervalMs": 10, "commandTimeoutMs": 500, "connectWaitMs": 0,
                     "link": { "type": "simulated", "simulator": {
                         "identity": [ "[VER:1.1f.20250101:]", "[FIRMWARE:grblHAL]" ], "axisLetters": [ "X", "Y", "Z", "A" ] } } } ],
      "heads": [ { "id": "H", "name": "Head" } ],
      "motionPlanner": { "continuousMotion": true },
      "axes": [ { "id": "X", "name": "x", "kind": "controller", "type": "x", "driver": "D", "letter": "X", "feedratePerSecond": 1000 },
                { "id": "Y", "name": "y", "kind": "controller", "type": "y", "driver": "D", "letter": "Y", "feedratePerSecond": 1000 },
                { "id": "Z", "name": "z", "kind": "controller", "type": "z", "driver": "D", "letter": "Z", "feedratePerSecond": 100,
                  "safeZone": { "low": 0, "high": 0, "lowEnabled": true, "highEnabled": true } },
                { "id": "C", "name": "c", "kind": "controller", "type": "rotation", "driver": "D", "letter": "A",
                  "feedratePerSecond": 4000, "wrapAroundRotation": true, "limitRotation": true, "resolution": 0.05625 } ],
      "nozzles": [ { "id": "N", "name": "Left", "mount": { "head": "H", "axisX": "X", "axisY": "Y", "axisZ": "Z", "axisRotation": "C" } } ]
    })";
    JPCellConfig config;
    assert(config.fromJson(JJson::parse(json), error));
    JPCell cell(config, { profile });
    std::mutex m;
    std::vector<std::string> turns;   // the moves and position settings naming A
    cell.onTraffic.connect([&](std::string, bool out, std::string line) {
        std::lock_guard lk(m);
        if (out && line.find(" A") != std::string::npos && (line.rfind("G1", 0) == 0 || line.rfind("G92", 0) == 0)) turns.push_back(line);
    });
    cell.connect();
    for (int i = 0; i < 300 && !cell.isConnected(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    cell.home();
    for (int i = 0; i < 300 && !cell.isHomed(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    assert(cell.isHomed());
    const JPMountConfig& nozzle = cell.config().nozzles[0].mount;
    std::string why;
    {
        std::lock_guard lk(m);
        turns.clear();
    }
    assert(cell.moveToolAndWait(nozzle, { 10, 10, -5, -180.1 }, 1, why));    // a pick, just past -180: 179.9
    assert(cell.moveToolAndWait(nozzle, { 50, 50, -3, -90 }, 1, why));       // the short way: 270, then told -90
    assert(cell.moveToolAndWait(nozzle, { 50.1, 50.2, -3, -88.8 }, 1, why)); // a degree and a bit more
    std::lock_guard lk(m);
    assert(turns.size() == 4);
    assert(turns[0].rfind("G1 A179.8875", 0) == 0);
    assert(turns[1].rfind("G1 A270.0000", 0) == 0);
    assert(turns[2] == "G92 A-90.0000");
    assert(turns[3].rfind("G1 A-88.8188", 0) == 0);
    cell.disconnect();
    return 0;
}
