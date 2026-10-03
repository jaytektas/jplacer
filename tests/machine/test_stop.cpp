// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// Stopping a move under way on a simulated controller whose moves last (its
// wait for motion is not answered): a stop holds it, throws its queue away
// and keeps it homed; an emergency stop resets it at once and the machine
// must be homed again. Either way the move fails with "stopped". A hold that
// never comes to rest is reset anyway, and the machine is unhomed. A
// controller left in alarm connects, and homing unlocks it.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "machine/JPCell.h"

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

using namespace jf;

namespace {

JPCellConfig cellConfig(bool holdNeverStill = false, bool alarm = false) {
    const std::string json = R"({
      "name": "Stop",
      "drivers": [ { "id": "D", "name": "Gantry", "statusIntervalMs": 10, "commandTimeoutMs": 1000, "connectWaitMs": 0,
                     "link": { "type": "simulated", "simulator": {
                         "identity": [ "[VER:1.1f.20250101:]", "[FIRMWARE:grblHAL]" ],
                         "axisLetters": [ "X" ], "stallDwell": true, "holdNeverStill": HOLD, "alarm": ALARM } } } ],
      "axes": [ { "id": "X", "name": "x", "kind": "controller", "type": "x", "driver": "D", "letter": "X", "feedratePerSecond": 100 } ]
    })";
    std::string text = json;
    text.replace(text.find("HOLD"), 4, holdNeverStill ? "true" : "false");
    text.replace(text.find("ALARM"), 5, alarm ? "true" : "false");
    JPCellConfig c;
    std::string error;
    const bool ok = c.fromJson(JJson::parse(text), error);
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
        until([&] { std::lock_guard lk(motion.m); return motion.last.has_value(); });
        std::lock_guard lk(motion.m);
        auto r = *motion.last;
        motion.last.reset();
        return r;
    };
    cell.connect();
    assert(until([&] { return cell.isConnected(); }));
    cell.home();
    assert(until([&] { return cell.isHomed(); }));
    take();

    // A move that lasts, stopped: held, its queue thrown away, still homed.
    cell.moveAxes({ { "X", 50.0 } }, 1.0);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    assert(cell.isMoving());
    std::string why;
    assert(cell.stop(false, why));
    const auto [ok, whyStopped] = take();
    assert(!ok && whyStopped.find("stopped") != std::string::npos && cell.isHomed() && !cell.isMoving());

    // Another, stopped in an emergency: reset at once, and homing is needed.
    cell.moveAxes({ { "X", 10.0 } }, 1.0);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    assert(cell.stop(true, why));
    const auto [ok2, why2] = take();
    assert(!ok2 && why2.find("emergency stop") != std::string::npos && !cell.isHomed());
    // Reset mid-move, the controller is in alarm, refusing G-code; homing
    // unlocks it first.
    assert(until([&] { return cell.inAlarm(); }));
    cell.home();
    assert(until([&] { return cell.isHomed(); }));
    assert(take().first && !cell.inAlarm());

    // A controller that never holds still: reset anyway once the wait is
    // up, and, its place perhaps lost, the machine is homed no longer.
    {
        JPCell slow(cellConfig(true), profiles());
        slow.onMotion.connect([&](bool ok, std::string why) {
            std::lock_guard lk(motion.m);
            motion.last = { ok, why };
        });
        std::string alarm;
        std::mutex am;
        slow.onAlarm.connect([&](std::string what) { std::lock_guard lk(am); alarm += what + "\n"; });
        slow.connect();
        assert(until([&] { return slow.isConnected(); }));
        slow.home();
        assert(until([&] { return slow.isHomed(); }));
        take();
        slow.moveAxes({ { "X", 50.0 } }, 1.0);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        assert(slow.stop(false, why));
        const auto [ok3, why3] = take();
        assert(!ok3 && why3.find("stopped") != std::string::npos);
        assert(until([&] { return !slow.isHomed(); }));
        std::lock_guard lk(am);
        assert(alarm.find("home the machine again") != std::string::npos);
    }

    // A controller found in alarm when connecting: connected all the same
    // (its start-up command waits), and homing unlocks it and sends it.
    {
        JPCell locked(cellConfig(false, true), profiles());
        locked.onMotion.connect([&](bool ok, std::string why) {
            std::lock_guard lk(motion.m);
            motion.last = { ok, why };
        });
        locked.connect();
        assert(until([&] { return locked.isConnected(); }));
        assert(until([&] { return locked.inAlarm(); }));
        locked.home();
        assert(until([&] { return locked.isHomed(); }));
        assert(take().first && !locked.inAlarm());
    }

    std::puts("test_stop: ok");
    return 0;
}
