// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// A nozzle tip's approach (JPNozzleTipConfig::approachHeightMm): a pick or place goes down at the Z axis's speed to
// its height above, the last of the way at its Approach Speed, and after the vacuum the same distance up at its Lift
// Speed, then up to safe Z. Without one, straight down and up at the Z axis's speed. A nozzle sent to a working
// height (the Jog panel's Position Nozzle) goes the last of the way down at its Approach Speed too; one sent to safe Z
// goes straight. The tip's file keeps it.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "machine/JPCell.h"

#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace jf;

namespace {

JPCellConfig cellConfig(double approach) {
    const std::string json = R"({
      "name": "Approach",
      "drivers": [ { "id": "D", "name": "Gantry", "statusIntervalMs": 10, "commandTimeoutMs": 500, "connectWaitMs": 0,
                     "link": { "type": "simulated", "simulator": {
                         "identity": [ "[VER:1.1f.20250101:]", "[FIRMWARE:grblHAL]" ], "axisLetters": [ "X", "Y", "Z" ] } } } ],
      "heads": [ { "id": "H", "name": "Head" } ],
      "axes": [ { "id": "X", "name": "x", "kind": "controller", "type": "x", "driver": "D", "letter": "X", "feedratePerSecond": 100 },
                { "id": "Y", "name": "y", "kind": "controller", "type": "y", "driver": "D", "letter": "Y", "feedratePerSecond": 100 },
                { "id": "Z", "name": "z", "kind": "controller", "type": "z", "driver": "D", "letter": "Z", "feedratePerSecond": 50,
                  "safeZone": { "low": 0, "high": 0, "lowEnabled": true, "highEnabled": false } } ],
      "actuators": [ { "id": "V", "name": "Vacuum", "driver": "D", "onCommand": "M64 P1", "offCommand": "M65 P1" } ],
      "nozzleTips": [ { "id": "T", "name": "Tip", "approach": { "height": )" + std::to_string(approach) + R"(, "speed": 20, "liftSpeed": 50 } } ],
      "nozzles": [ { "id": "N", "name": "N1", "tip": "T", "tips": [ "T" ], "vacuumActuator": "V",
                     "mount": { "head": "H", "axisX": "X", "axisY": "Y", "axisZ": "Z" } } ]
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

// The Z moves and the vacuum switched, as sent, from a place at Z -6.
std::vector<std::string> placed(double approach) {
    JPCell cell(cellConfig(approach), profiles());
    cell.connect();
    for (int i = 0; i < 300 && !cell.isConnected(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    cell.home();
    for (int i = 0; i < 300 && !cell.isHomed(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    assert(cell.isHomed());
    std::mutex m;
    std::vector<std::string> sent;
    auto watch = cell.onTraffic.connect([&](std::string, bool out, std::string line) {
        if (!out || (line.rfind("G1 Z", 0) != 0 && line.rfind("M65", 0) != 0)) return;
        std::lock_guard lk(m);
        sent.push_back(line);
    });
    std::string why;
    const bool ok = cell.placeAtAndWait("N", { 10.0, 10.0, -6.0, std::nullopt }, 1.0, why);
    assert(ok);
    watch();
    cell.disconnect();
    return sent;
}

// The Z moves as sent for the nozzle moved (as the Jog panel's Position Nozzle moves it) to Z `z`.
std::vector<std::string> movedTo(double approach, double z) {
    JPCellConfig config = cellConfig(approach);
    JPCell cell(config, profiles());
    cell.connect();
    for (int i = 0; i < 300 && !cell.isConnected(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    cell.home();
    for (int i = 0; i < 300 && !cell.isHomed(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    assert(cell.isHomed());
    std::mutex m;
    std::vector<std::string> sent;
    auto watch = cell.onTraffic.connect([&](std::string, bool out, std::string line) {
        if (!out || line.rfind("G1 Z", 0) != 0) return;
        std::lock_guard lk(m);
        sent.push_back(line);
    });
    cell.moveTool(cell.config().nozzles[0].mount, { 10.0, 10.0, z, std::nullopt }, 1.0);
    for (int i = 0; i < 300 && cell.isMoving(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    for (int i = 0; i < 300 && cell.isMoving(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    watch();
    cell.disconnect();
    return sent;
}

} // namespace

int main() {
    // 1 mm of approach: down at full speed (F3000) to -5, the last mm at 20% (F600), the vacuum off, up the mm at
    // 50% (F1500), then to safe Z at full speed.
    const std::vector<std::string> with = placed(1);
    assert(with.size() == 5);
    assert(with[0] == "G1 Z-5.0000 F3000" && with[1] == "G1 Z-6.0000 F600" && with[2].rfind("M65", 0) == 0);
    assert(with[3] == "G1 Z-5.0000 F1500" && with[4] == "G1 Z0.0000 F3000");
    // None: straight down and up.
    const std::vector<std::string> without = placed(0);
    assert(without.size() == 3 && without[0] == "G1 Z-6.0000 F3000" && without[1].rfind("M65", 0) == 0
           && without[2] == "G1 Z0.0000 F3000");
    // Moved down to a working height: to its height above at full speed, the last mm at 20%. To safe Z: straight.
    const std::vector<std::string> down = movedTo(1, -6);
    assert(down.size() == 2 && down[0] == "G1 Z-5.0000 F3000" && down[1] == "G1 Z-6.0000 F600");
    const std::vector<std::string> safe = movedTo(1, 0);
    assert(safe.empty() || (safe.size() == 1 && safe[0] == "G1 Z0.0000 F3000"));
    const std::vector<std::string> plain = movedTo(0, -6);
    assert(plain.size() == 1 && plain[0] == "G1 Z-6.0000 F3000");
    // The tip's file keeps it (none: nothing written).
    const JPCellConfig c = cellConfig(1.5);
    const JPNozzleTipConfig back = JPNozzleTipConfig::fromJson(c.nozzleTips[0].toJson());
    assert(back.approachHeightMm == 1.5 && back.approachSpeedPercent == 20 && back.liftSpeedPercent == 50);
    assert(cellConfig(0).nozzleTips[0].toJson()["approach"].isObject() == true);   // speeds not the default
    return 0;
}
