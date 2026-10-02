// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// A gantry whose Y leans: jplacer works in square coordinates and the axes
// are told their own. A move to a square point sends X less the lean for its
// Y; a move along Y alone takes X along to stay square; positions come back
// square; homing and a correction of the position go through the same lean.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "machine/JPCell.h"

#include <chrono>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <thread>

using namespace jf;

namespace {

constexpr double kLean = 0.01;   // mm of X per mm of Y: far more than any real gantry, to see it

JPCellConfig cellConfig(const std::string& axisY) {
    const std::string json = R"({
      "name": "Test",
      "drivers": [ { "id": "D", "name": "Gantry", "statusIntervalMs": 10, "commandTimeoutMs": 500, "connectWaitMs": 0,
                     "link": { "type": "simulated", "simulator": {
                         "identity": [ "[VER:1.1f.20250101:]", "[FIRMWARE:grblHAL]" ], "axisLetters": [ "X", "Y" ] } } } ],
      "axes": [ { "id": "X", "name": "x", "kind": "controller", "type": "x", "driver": "D", "letter": "X",
                  "homeCoordinate": 390, "feedratePerSecond": 100 },
                { "id": "Y", "name": "y", "kind": "controller", "type": "y", "driver": "D", "letter": "Y",
                  "homeCoordinate": 444, "feedratePerSecond": 100 } ],
      "squareness": { "axisX": "X", "axisY": ")" + axisY + R"(", "xPerY": 0.01 }
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
    std::optional<bool> value;
    void set(bool v) { { std::lock_guard lk(m); value = v; } cv.notify_all(); }
    bool take() {
        std::unique_lock lk(m);
        const bool got = cv.wait_for(lk, std::chrono::seconds(3), [&] { return value.has_value(); });
        assert(got);
        const bool v = *value;
        value.reset();
        return v;
    }
};

bool near(double a, double b) { return std::abs(a - b) < 1e-6; }

} // namespace

int main() {
    // An axis that is not a controller's cannot lean.
    assert(!cellConfig("Z").problems().empty());
    const JPCellConfig config = cellConfig("Y");
    assert(config.problems().empty() && config.squareness.active());

    JPCell cell(config, profiles());
    Latch connected, homed;
    cell.onConnection.connect([&](bool ok, std::string) { connected.set(ok); });
    cell.onMotion.connect([&](bool ok, std::string) { homed.set(ok); });
    std::vector<std::string> sent;
    std::mutex sentMutex;
    cell.onTraffic.connect([&](std::string, bool out, std::string line) {
        if (!out || (line.rfind("G1", 0) != 0 && line.rfind("G92", 0) != 0)) return;
        std::lock_guard lk(sentMutex);
        sent.push_back(line);
    });
    auto lastSent = [&] {
        std::lock_guard lk(sentMutex);
        return sent.empty() ? std::string() : sent.back();
    };
    cell.connect();
    assert(connected.take());

    // Home: the axes at their home coordinates; squarely, X takes the lean for Y 444.
    cell.home();
    assert(homed.take());
    assert(near(cell.jogBase().at("X"), 390 + kLean * 444) && near(cell.jogBase().at("Y"), 444));

    // To a square point: X less the lean for its Y.
    std::string why;
    assert(cell.moveAxesAndWait({ { "X", 100 }, { "Y", 200 } }, 1.0, why));
    assert(lastSent().rfind("G1 X98.0000 Y200.0000", 0) == 0);
    const auto p = cell.positions();
    assert(near(p.at("X"), 100) && near(p.at("Y"), 200));

    // Along Y alone: X goes along so the head stays at square X 100.
    assert(cell.moveAxesAndWait({ { "Y", 300 } }, 1.0, why));
    assert(lastSent().rfind("G1 X97.0000 Y300.0000", 0) == 0);
    assert(near(cell.positions().at("X"), 100) && near(cell.jogBase().at("X"), 100));

    // A correction: square X 100 becomes 99.5; the controller is told its own coordinates.
    assert(cell.correctPosition({ { "X", 0.5 } }, why));
    assert(lastSent() == "G92 X96.5000");   // Y unchanged: only X is told
    assert(near(cell.positions().at("X"), 99.5) && near(cell.positions().at("Y"), 300));

    cell.disconnect();

    // Pivoted at Y 200: there the axes' X and square X agree.
    JPCellConfig pivoted = cellConfig("Y");
    pivoted.squareness.atY = 200;
    JPCell other(pivoted, profiles());
    Latch otherConnected, otherHomed;
    other.onConnection.connect([&](bool ok, std::string) { otherConnected.set(ok); });
    other.onMotion.connect([&](bool ok, std::string) { otherHomed.set(ok); });
    other.onTraffic.connect([&](std::string, bool out, std::string line) {
        if (!out || line.rfind("G1", 0) != 0) return;
        std::lock_guard lk(sentMutex);
        sent.push_back(line);
    });
    other.connect();
    assert(otherConnected.take());
    other.home();
    assert(otherHomed.take());
    assert(near(other.jogBase().at("X"), 390 + kLean * (444 - 200)));
    assert(other.moveAxesAndWait({ { "X", 100 }, { "Y", 200 } }, 1.0, why));
    assert(lastSent().rfind("G1 X100.0000 Y200.0000", 0) == 0);
    assert(other.moveAxesAndWait({ { "Y", 300 } }, 1.0, why));
    assert(lastSent().rfind("G1 X99.0000 Y300.0000", 0) == 0);
    other.disconnect();
    return 0;
}
