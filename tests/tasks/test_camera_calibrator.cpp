// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// Calibration and the visual test end to end, with no hardware: a cell over a
// simulated controller, and a simulated head camera that draws the homing mark
// where the head's position puts it, through a transform only the scene knows.
// The calibrator must recover that transform from its moves, and the visual
// test must then find the mark where the scene put it, not where the head's
// settings say it is.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "tasks/JPCameraCalibrator.h"
#include "tasks/JPVisualTest.h"

#include <chrono>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <thread>

using namespace jf;

namespace {

// Hidden from everything but the scene: looking down, turned a quarter degree,
// a little non-square.
constexpr double kM[4] = { -25.7, -0.11, -0.11, 25.6 };
// Where the mark really is, and where the head's settings say it is.
constexpr double kMarkX = 137.237, kMarkY = 179.165;
constexpr double kSetX  = 137.137, kSetY  = 179.265;

JPCellConfig cellConfig() {
    const std::string json = R"({
      "name": "Test",
      "drivers": [ { "id": "D", "name": "Gantry", "statusIntervalMs": 10, "commandTimeoutMs": 500, "connectWaitMs": 0,
                     "link": { "type": "simulated", "simulator": {
                         "identity": [ "[VER:1.1f.20250101:]", "[FIRMWARE:grblHAL]" ],
                         "axisLetters": [ "X", "Y" ] } } } ],
      "heads": [ { "id": "H", "name": "Head", "homingFiducial": { "x": 137.137, "y": 179.265 },
                   "homingFiducialDiameter": 1.85 } ],
      "axes": [ { "id": "X", "name": "x", "kind": "controller", "type": "x", "driver": "D", "letter": "X",
                  "homeCoordinate": 390, "feedratePerSecond": 500,
                  "backlash": "oneSided", "backlashOffset": 0.1, "backlashSpeedFactor": 0.25 },
                { "id": "Y", "name": "y", "kind": "controller", "type": "y", "driver": "D", "letter": "Y",
                  "homeCoordinate": 444, "feedratePerSecond": 500,
                  "backlash": "oneSided", "backlashOffset": 0.1, "backlashSpeedFactor": 0.25 } ],
      "cameras": [ { "id": "C", "name": "Top", "mount": { "head": "H", "axisX": "X", "axisY": "Y" },
                     "device": { "backend": "simulated", "width": 640, "height": 480, "fps": 60,
                       "scene": { "pxPerMm": [-25.7, -0.11, -0.11, 25.6], "ground": 30, "mark": 190, "noise": 3,
                                  "marks": [ { "x": 137.237, "y": 179.165, "diameter": 1.85 } ] } } } ]
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

// Waits for a signal's value, set from another thread.
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

} // namespace

int main() {
    JPCell cell(cellConfig(), profiles());
    Latch connected, homed;
    cell.onConnection.connect([&](bool ok, std::string) { connected.set(ok); });
    cell.onMotion.connect([&](bool ok, std::string) { homed.set(ok); });
    cell.connect();
    assert(connected.take());

    const JPCameraConfig& camConfig = cell.config().cameras.front();
    JPCameraFeed feed(camConfig);
    feed.setView([&](double& x, double& y) {
        const auto p = cell.positions();
        x = p.at("X");
        y = p.at("Y");
        return true;
    });
    feed.start();

    // Not homed: refused.
    std::string why;
    JPCameraCalibrator::Options o;
    o.markDiameterMm = 1.85;
    assert(!JPCameraCalibrator::run(cell, feed, o, why) && why.find("home") != std::string::npos);

    cell.home();
    assert(homed.take());

    // Not calibrated: the visual test says so.
    const JPHeadConfig& head = cell.config().heads.front();
    JPVisualTest::Result t = JPVisualTest::run(cell, feed, head, 1.0);
    assert(!t.found && t.why.find("calibrate") != std::string::npos);

    // Roughly over the mark (not exactly: the first look must find it off centre).
    assert(cell.moveAxesAndWait({ { "X", kMarkX + 0.4 }, { "Y", kMarkY - 0.3 } }, 1.0, why));
    o.speed = 1.0;
    const auto cal = JPCameraCalibrator::run(cell, feed, o, why);
    assert(cal && cal->valid);
    for (int i = 0; i < 4; ++i) assert(std::abs(cal->pxPerMm[i] - kM[i]) < 0.01);
    assert(cal->rmsPx < 0.05);
    // It went back where it began.
    const auto base = cell.jogBase();
    assert(std::abs(base.at("X") - (kMarkX + 0.4)) < 1e-6 && std::abs(base.at("Y") - (kMarkY - 0.3)) < 1e-6);

    // A mark of another size is not the mark it was told about.
    JPCameraCalibrator::Options wrong = o;
    wrong.markDiameterMm = 3.0;
    assert(!JPCameraCalibrator::run(cell, feed, wrong, why) && why.find("expected") != std::string::npos);

    // Calibrated, the visual test finds the mark where the scene put it.
    cell.setCameraCalibration("C", *cal);
    t = JPVisualTest::run(cell, feed, head, 1.0);
    assert(t.found);
    assert(std::abs(t.markX - kMarkX) < 0.002 && std::abs(t.markY - kMarkY) < 0.002);
    assert(std::abs(t.offsetX - (kMarkX - kSetX)) < 0.002 && std::abs(t.offsetY - (kMarkY - kSetY)) < 0.002);

    feed.stop();
    cell.disconnect();
    return 0;
}
