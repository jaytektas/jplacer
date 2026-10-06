// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// OpenPnP's motion planner, as jplacer runs it. Continuous motion: the moves
// of one piece of work go to the controller back to back, waited for once at
// its end; without it, each is waited for. An actuator coordinates with the
// machine as it says: waiting for the moves before it to finish, or not.
// Test Motion: the tool through the enabled places from the first, its
// planned time from the axes' feed rates and accelerations, and the time
// it took.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "machine/JPCell.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

using namespace jf;

namespace {

JPCellConfig cellConfig(bool continuous) {
    const std::string json = R"({
      "name": "Test",
      "drivers": [ { "id": "D", "name": "Gantry", "statusIntervalMs": 10, "commandTimeoutMs": 500, "connectWaitMs": 0,
                     "link": { "type": "simulated", "simulator": {
                         "identity": [ "[VER:1.1f.20250101:]", "[FIRMWARE:grblHAL]" ], "axisLetters": [ "X", "Y", "Z", "A" ] } } } ],
      "heads": [ { "id": "H", "name": "Head" } ],
      "axes": [ { "id": "X", "name": "x", "kind": "controller", "type": "x", "driver": "D", "letter": "X",
                  "feedratePerSecond": 100, "accelerationPerSecond2": 1000 },
                { "id": "Y", "name": "y", "kind": "controller", "type": "y", "driver": "D", "letter": "Y",
                  "feedratePerSecond": 100, "accelerationPerSecond2": 1000 },
                { "id": "Z", "name": "z", "kind": "controller", "type": "z", "driver": "D", "letter": "Z",
                  "feedratePerSecond": 50 },
                { "id": "C", "name": "c", "kind": "controller", "type": "rotation", "driver": "D", "letter": "A",
                  "feedratePerSecond": 360 } ],
      "actuators": [ { "id": "A", "name": "Valve", "driver": "D", "onCommand": "M64 P0", "offCommand": "M65 P0",
                       "interlock": { "enabled": true, "type": "SignalAxesMoving", "axes": [ "Y" ] } } ],
      "cameras": [ { "id": "C", "name": "Top", "mount": { "head": "H", "axisX": "X", "axisY": "Y" },
                     "device": { "backend": "simulated", "width": 64, "height": 48, "fps": 10 } } ]
    })";
    JPCellConfig c;
    std::string error;
    const bool ok = c.fromJson(JJson::parse(json), error);
    assert(ok && c.problems().empty());
    c.motionPlanner.continuousMotion = continuous;
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

// The lines sent, each move as "move" and each wait for the machine as "wait".
struct Sent {
    std::mutex m;
    std::vector<std::string> lines;
    void add(const std::string& line) {
        std::lock_guard lk(m);
        if (line.rfind("G1", 0) == 0 || line.rfind("G0", 0) == 0) lines.push_back("move");
        else if (line == "G4 P0") lines.push_back("wait");
        else if (line.rfind("M64", 0) == 0) lines.push_back("valve on");
        else if (line.rfind("M65", 0) == 0) lines.push_back("valve off");
    }
    std::vector<std::string> take() {
        std::lock_guard lk(m);
        auto out = std::move(lines);
        lines.clear();
        return out;
    }
};

// A connected, homed cell that tells `sent` what it sends.
void start(JPCell& cell, Sent& sent) {
    auto connected = std::make_shared<Latch>();
    cell.onConnection.connect([connected](bool ok, std::string w) { connected->set(ok, w); });
    cell.onTraffic.connect([&sent](const std::string&, bool out, const std::string& line) {
        if (out) sent.add(line);
    });
    cell.connect();
    assert(connected->take().first);
    std::string why;
    cell.home();
    for (int i = 0; i < 300 && !cell.isHomed(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    assert(cell.isHomed());
}

using Lines = std::vector<std::string>;

} // namespace

int main() {
    const JPMountConfig camera = cellConfig(false).cameras[0].mount;
    for (const bool continuous : { false, true }) {
        JPCellConfig plain = cellConfig(continuous);
        plain.actuators.clear();
        JPCell cell(plain, profiles());
        Sent sent;
        start(cell, sent);
        sent.take();
        std::string why;
        // Two moves straight in one piece of work.
        assert(cell.moveToolAndWait(camera, { 10.0, 20.0, std::nullopt, std::nullopt }, 1.0, why));
        const Lines one = sent.take();
        assert(one == (Lines{ "move", "wait" }));
        // Back to back, then waited for once (continuous); each waited for (not).
        auto config = cell.config();
        JPMotionPlannerConfig& mp = config.motionPlanner;
        mp.stops[0] = { true, { 0, 0, 0, 0 } };
        mp.stops[1] = { true, { 100, 0, 0, 0 } };
        mp.stops[2] = { false, { 50, 50, 0, 0 } };
        mp.stops[3] = { true, { 100, 100, 0, 0 } };
        mp.safeZ = { false, false, false };
        mp.speeds = { 1, 1, 1 };
        assert(cell.reconfigure(config, why));
        JPMotionTestResult result;
        assert(cell.testMotionAndWait(camera, false, result, why));
        const Lines run = sent.take();
        // To the first place and standing there (one wait, after the move, when it was not waited for); then the run.
        assert(continuous ? run == (Lines{ "move", "wait", "move", "move", "wait" })
                          : run == (Lines{ "move", "wait", "wait", "move", "wait", "move", "wait" }));
        // 100 mm at 100 mm/s, speeding up and slowing down at 1000 mm/s²: 1.1 s each leg.
        assert(std::abs(result.plannedS - 2.2) < 1e-9);
        assert(result.actualS > 0);
        // The third place is skipped; its leg's speed is the one into the last place's.
        assert(cell.jogBase().at("X") == 100 && cell.jogBase().at("Y") == 100);
        // Reverse: from the last back to the first.
        assert(cell.testMotionAndWait(camera, true, result, why));
        assert(cell.jogBase().at("X") == 0 && cell.jogBase().at("Y") == 0);
        cell.disconnect();
    }
    for (const std::string before : { "WaitForStillstand", "None" }) {
        // An actuator switched within a piece of work (signalling Y moving):
        // after the move, waiting for it to finish first, or not.
        JPCellConfig config = cellConfig(true);
        config.actuators[0].coordinatedBeforeActuate = before;
        JPCell cell(config, profiles());
        Sent sent;
        start(cell, sent);
        sent.take();
        std::string why;
        assert(cell.moveAxesAndWait({ { "Y", 30 } }, 1.0, why));
        const Lines lines = sent.take();
        assert(before == "None" ? lines == (Lines{ "valve on", "move", "valve off", "wait" })
                                : lines == (Lines{ "valve on", "move", "wait", "valve off" }));
        cell.disconnect();
    }
    {
        // The feed rate as G-code reads F: over the linear axes' path, the move as long as its slowest
        // axis takes; over the rotation's when nothing linear moves; Switch Linear <-> Rotational heeded.
        JPCellConfig config = cellConfig(false);
        config.actuators.clear();
        JPCell cell(config, profiles());
        Sent sent;
        start(cell, sent);
        std::vector<std::string> raw;
        std::mutex m;
        cell.onTraffic.connect([&](const std::string&, bool out, const std::string& line) {
            std::lock_guard lk(m);
            if (out && line.rfind("G1", 0) == 0) raw.push_back(line);
        });
        std::string why;
        auto last = [&] {
            std::lock_guard lk(m);
            return raw.back();
        };
        // X 30 mm and Y 40 mm at 100 mm/s each: 0.4 s, 50 mm of path: F7500.
        assert(cell.moveAxesAndWait({ { "X", 30 }, { "Y", 40 } }, 1.0, why) && last().find("F7500") != std::string::npos);
        // X 30 mm with C 180° at 360°/s: 0.5 s (the turn), 30 mm of linear path: F3600.
        assert(cell.moveAxesAndWait({ { "X", 60 }, { "C", 180 } }, 1.0, why) && last().find("F3600") != std::string::npos);
        // C alone 90° at 360°/s: degrees a minute, F21600.
        assert(cell.moveAxesAndWait({ { "C", 90 } }, 1.0, why) && last().find("F21600") != std::string::npos);
        // C switched to linear on the controller, moved with X: its path counts with X's.
        config = cell.config();
        for (JPAxisConfig& a : config.axes)
            if (a.id == "C") a.switchLinearRotational = true;
        assert(cell.reconfigure(config, why));
        // X 30 (0.3 s) and C 40 (0.11 s): 0.3 s over 50 units of path: F10000.
        assert(cell.moveAxesAndWait({ { "X", 90 }, { "C", 130 } }, 1.0, why) && last().find("F10000") != std::string::npos);
        cell.disconnect();
    }
    {
        // Undefined: none enabled.
        JPCell cell(cellConfig(true), profiles());
        Sent sent;
        start(cell, sent);
        JPMotionTestResult result;
        std::string why;
        assert(!cell.testMotionAndWait(camera, false, result, why) && why.find("Test Motion undefined") == 0);
        cell.disconnect();
    }
    {
        // OpenPnP's Allow uncoordinated?: on a controller with full 3rd order motion control, with continuous
        // motion, a nozzle's legs by way of safe Z (up, across, down) planned as one sequence, the move across
        // within the Safe Zone uncoordinated, blended: the planned run shorter than its moves one by one, and the
        // nozzle where it was sent.
        double planned[2] {};
        for (const bool uncoordinated : { false, true }) {
            JPCellConfig config = cellConfig(true);
            config.actuators.clear();
            config.drivers[0].motionControlType = "Full3rdOrderControl";
            for (JPAxisConfig& a : config.axes) {
                if (a.id == "C") continue;
                a.accelerationPerSecond2 = 1000;
                a.jerkPerSecond3 = 20000;
                if (a.id == "Z") {
                    a.safeZoneLow = -5;
                    a.safeZoneHigh = 5;
                    a.safeZoneLowEnabled = a.safeZoneHighEnabled = true;
                }
            }
            config.nozzles.push_back(JPNozzleConfig::fromJson(JJson::parse(
                R"({ "id": "N", "name": "N1", "mount": { "head": "H", "axisX": "X", "axisY": "Y", "axisZ": "Z" } })")));
            JPMotionPlannerConfig& mp = config.motionPlanner;
            mp.allowUncoordinated = uncoordinated;
            mp.stops[0] = { true, { 0, 0, -10, 0 } };
            mp.stops[1] = { true, { 100, 0, -10, 0 } };
            mp.stops[2] = { false, { 0, 0, 0, 0 } };
            mp.stops[3] = { false, { 0, 0, 0, 0 } };
            mp.safeZ = { true, true, true };
            mp.speeds = { 1, 1, 1 };
            JPCell cell(config, profiles());
            Sent sent;
            start(cell, sent);
            std::string why;
            JPMotionTestResult result;
            assert(cell.testMotionAndWait(config.nozzles[0].mount, false, result, why));
            planned[uncoordinated] = result.plannedS;
            assert(result.plannedS > 0);
            // Where it was sent (then up to safe Z, as Test Motion ends).
            assert(std::abs(cell.jogBase().at("X") - 100) < 1e-6 && std::abs(cell.jogBase().at("Y")) < 1e-6);
            cell.disconnect();
        }
        std::fprintf(stderr, "planned one by one %.3f s, blended %.3f s\n", planned[0], planned[1]);
        assert(planned[1] < planned[0] - 0.01);
    }
    return 0;
}
