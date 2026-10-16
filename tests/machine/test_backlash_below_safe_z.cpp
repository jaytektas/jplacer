// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// Backlash Off Below Safe Z (JPCell::setBacklashOffBelowSafeZ), decided as each move is sent: a jog made with the
// nozzle's Z below its safe zone goes straight to its place; any other move made then (a job's, a camera task's) is
// compensated, past the place and back; the move right after going up is compensated (before, the Jog panel turned
// compensation back on a moment late, and a job's place move after bottom vision went uncompensated); a jog at safe
// Z is compensated; the option off, a jog below safe Z is compensated too.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "machine/JPCell.h"

#include <chrono>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace jf;

namespace {

JPCellConfig cellConfig() {
    const std::string json = R"({
      "name": "Below",
      "drivers": [ { "id": "D", "name": "Gantry", "statusIntervalMs": 10, "commandTimeoutMs": 500, "connectWaitMs": 0,
                     "link": { "type": "simulated", "simulator": {
                         "identity": [ "[VER:1.1f.20250101:]", "[FIRMWARE:grblHAL]" ], "axisLetters": [ "X", "Y", "Z" ] } } } ],
      "heads": [ { "id": "H", "name": "Head" } ],
      "axes": [ { "id": "X", "name": "x", "kind": "controller", "type": "x", "driver": "D", "letter": "X", "feedratePerSecond": 1000 },
                { "id": "Y", "name": "y", "kind": "controller", "type": "y", "driver": "D", "letter": "Y", "feedratePerSecond": 1000,
                  "backlash": "oneSided", "backlashOffset": 0.1 },
                { "id": "Z", "name": "z", "kind": "controller", "type": "z", "driver": "D", "letter": "Z", "feedratePerSecond": 500,
                  "safeZone": { "low": 0, "high": 0, "lowEnabled": true, "highEnabled": false } } ],
      "nozzles": [ { "id": "N", "name": "N1", "mount": { "head": "H", "axisX": "X", "axisY": "Y", "axisZ": "Z" } } ]
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

} // namespace

int main() {
    JPCell cell(cellConfig(), profiles());
    cell.connect();
    for (int i = 0; i < 300 && !cell.isConnected(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    cell.home();
    for (int i = 0; i < 300 && !cell.isHomed(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    assert(cell.isHomed() && cell.backlashCompensation());
    std::mutex m;
    std::vector<std::string> sent;
    auto watch = cell.onTraffic.connect([&](std::string, bool out, std::string line) {
        if (!out || line.rfind("G1 ", 0) != 0 || line.find(" Y") == std::string::npos) return;
        std::lock_guard lk(m);
        sent.push_back(line);
    });
    // How many lines with Y the move sent: two (past and back) compensated, one not.
    auto linesOf = [&](const std::function<void()>& move) {
        {
            std::lock_guard lk(m);
            sent.clear();
        }
        move();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        for (int i = 0; i < 300 && cell.isMoving(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        std::lock_guard lk(m);
        return sent.size();
    };
    std::string why;
    auto down = [&] { assert(cell.moveAxesAndWait({ { "Z", -5.0 } }, 1.0, why) && cell.headBelowSafeZ("H")); };
    cell.setBacklashOffBelowSafeZ(true);
    down();
    // A jog below safe Z: straight there.
    assert(linesOf([&] { cell.jog("N", 0, -1, 0, 0, 1.0); }) == 1);
    // Any other move below safe Z: compensated.
    assert(linesOf([&] { assert(cell.moveAxesAndWait({ { "Y", 10.0 } }, 1.0, why)); }) == 2);
    // Up to safe Z and straight on to a place, as a job's place after bottom vision: compensated.
    assert(linesOf([&] {
               assert(cell.moveToolAndWait(cell.config().nozzles[0].mount, { 5.0, 20.0, std::nullopt, std::nullopt }, 1.0, why));
           }) == 2);
    assert(!cell.headBelowSafeZ("H"));
    // A jog at safe Z: compensated.
    assert(linesOf([&] { cell.jog("N", 0, -1, 0, 0, 1.0); }) == 2);
    // The option off: a jog below safe Z compensated.
    cell.setBacklashOffBelowSafeZ(false);
    down();
    assert(linesOf([&] { cell.jog("N", 0, -1, 0, 0, 1.0); }) == 2);
    watch();
    cell.disconnect();
    return 0;
}
