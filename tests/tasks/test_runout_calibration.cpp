// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// A nozzle tip's runout, measured with a fixed camera looking up, with no
// hardware: in the simulated scene the tip's end swings round a circle as
// the nozzle turns (a hidden radius and phase), about an axis a little off
// where the nozzle's offset says. The calibrator must find the circle; with
// it compensated, moves to any angle put the tip's centre where they were sent
// (the axis's own offset aside, which is reported, not compensated).
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "tasks/JPRunoutCalibrator.h"

#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <mutex>

using namespace jf;

namespace {

constexpr double kM[4] = { 30.1, 0.12, 0.08, 30.0 };
constexpr double kCamX = 60, kCamY = 95;
constexpr double kOffX = 10, kOffY = -5;
// The hidden runout: radius, phase, and the axis's offset from where the nozzle's offset says.
constexpr double kRadius = 0.15, kPhase = 30, kAxisX = 0.04, kAxisY = -0.02;

JPCellConfig cellConfig() {
    char camera[512];
    std::snprintf(camera, sizeof camera, R"({ "id": "B", "name": "Bottom", "looking": "up",
        "mount": { "offset": { "x": %g, "y": %g, "z": -24 } },
        "device": { "backend": "simulated", "width": 640, "height": 480, "fps": 60,
          "scene": { "pxPerMm": [30.1, 0.12, 0.08, 30.0], "ground": 20, "mark": 180, "noise": 3,
                     "marks": [ { "x": %g, "y": %g, "diameter": 1.2 } ] } } })", kCamX, kCamY, kCamX, kCamY);
    const std::string json = R"({
      "name": "Test",
      "drivers": [ { "id": "D", "name": "Gantry", "statusIntervalMs": 10, "commandTimeoutMs": 500, "connectWaitMs": 0,
                     "link": { "type": "simulated", "simulator": {
                         "identity": [ "[VER:1.1f.20250101:]", "[FIRMWARE:grblHAL]" ], "axisLetters": [ "X", "Y", "Z", "A" ] } } } ],
      "heads": [ { "id": "H", "name": "Head" } ],
      "axes": [ { "id": "X", "name": "x", "kind": "controller", "type": "x", "driver": "D", "letter": "X",
                  "homeCoordinate": 390, "feedratePerSecond": 2000 },
                { "id": "Y", "name": "y", "kind": "controller", "type": "y", "driver": "D", "letter": "Y",
                  "homeCoordinate": 444, "feedratePerSecond": 2000 },
                { "id": "Z", "name": "z", "kind": "controller", "type": "z", "driver": "D", "letter": "Z",
                  "feedratePerSecond": 500, "safeZone": { "low": -1, "high": 1, "lowEnabled": true, "highEnabled": true } },
                { "id": "C", "name": "c", "kind": "controller", "type": "rotation", "driver": "D", "letter": "A",
                  "feedratePerSecond": 3600 } ],
      "nozzles": [ { "id": "N", "name": "Left", "mount": { "head": "H", "axisX": "X", "axisY": "Y", "axisZ": "Z",
                                                          "axisRotation": "C", "offset": { "x": 10, "y": -5, "z": 0 } },
                     "tips": [ "T" ], "tip": "T" } ],
      "nozzleTips": [ { "id": "T", "name": "503", "diameter": 1.2,
                        "runoutCalibration": { "enabled": true, "divisions": 8 } } ],
      "cameras": [ )" + std::string(camera) + R"( ]
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

struct Latch {
    std::mutex m;
    std::condition_variable cv;
    std::optional<bool> value;
    void set(bool v) { { std::lock_guard lk(m); value = v; } cv.notify_all(); }
    void clear() { std::lock_guard lk(m); value.reset(); }
    bool take() {
        std::unique_lock lk(m);
        const bool got = cv.wait_for(lk, std::chrono::seconds(3), [&] { return value.has_value(); });
        assert(got);
        const bool v = *value;
        value.reset();
        return v;
    }
};

// Where the tip's end really is for the axes at `p`.
void tipAt(const std::map<std::string, double>& p, double& x, double& y) {
    const double t = (p.at("C") - kPhase) * M_PI / 180;
    x = p.at("X") + kOffX + kAxisX + kRadius * std::cos(t);
    y = p.at("Y") + kOffY + kAxisY + kRadius * std::sin(t);
}

} // namespace

int main() {
    JPCell cell(cellConfig(), profiles());
    Latch connected, moved;
    cell.onConnection.connect([&](bool ok, std::string) { connected.set(ok); });
    cell.onMotion.connect([&](bool ok, std::string) { moved.set(ok); });
    cell.connect();
    assert(connected.take());
    cell.home();
    assert(moved.take());

    // The camera stays and sees the tip: drawn as a camera at 2C - tip
    // looking at a mark at C (as the bottom calibration's test does).
    JPCameraFeed feed(cell.config().cameras.front());
    feed.setView([&](double& x, double& y) {
        const auto p = cell.positions();
        if (!p.count("X") || !p.count("Y") || !p.count("C")) return false;
        double tx, ty;
        tipAt(p, tx, ty);
        x = 2 * kCamX - tx;
        y = 2 * kCamY - ty;
        return true;
    });
    feed.start();
    JPCameraCalibration cal;
    cal.valid = true;
    cal.pxPerMm = { kM[0], kM[1], kM[2], kM[3] };
    cal.width = 640;
    cal.height = 480;
    cell.setCameraCalibration("B", cal);

    std::string why;
    const auto r = JPRunoutCalibrator::run(cell, feed, cell.config().nozzles.front(), cell.config().nozzleTips.front(),
                                           JPRunoutCalibrator::Options{}, why);
    if (!r) std::fprintf(stderr, "why: %s\n", why.c_str());
    assert(r);
    assert(r->points.size() == 8);
    assert(std::abs(r->radius - kRadius) < 0.003 && std::abs(r->phaseDeg - kPhase) < 1.5);
    assert(std::abs(r->centreX - kAxisX) < 0.003 && std::abs(r->centreY - kAxisY) < 0.003 && r->rmsMm < 0.003);
    // Up again after.
    assert(std::abs(cell.positions().at("Z")) <= 1);

    // OpenPnP's Offset Threshold: a runout larger than it is a misdetect at every angle, and fails.
    {
        JPNozzleTipConfig tight = cell.config().nozzleTips.front();
        tight.runoutCalibration.offsetThresholdMm = 0.05;
        std::string tooFar;
        assert(!JPRunoutCalibrator::run(cell, feed, cell.config().nozzles.front(), tight, JPRunoutCalibrator::Options{}, tooFar));
        assert(tooFar.find("too many vision misdetects") != std::string::npos);
    }

    // Compensated: the tip's centre lands where it is sent, at any angle.
    JPCellConfig next = cell.config();
    next.nozzleTips.front().runout["N"] = *r;
    assert(cell.reconfigure(next, why));
    const JPMountConfig mount = cell.config().nozzles.front().mount;
    for (double angle : { 0.0, 90.0, -135.0, 170.0 }) {
        moved.clear();   // the calibrator's own moves said so too
        cell.moveTool(mount, { kCamX, kCamY, std::nullopt, angle }, 1.0);
        assert(moved.take());
        double tx, ty;
        const auto p = cell.positions();
        tipAt(p, tx, ty);
        assert(std::hypot(tx - kAxisX - kCamX, ty - kAxisY - kCamY) < 0.004);
    }
    // A turn alone keeps the centre where it is.
    moved.clear();
    cell.jog("N", 0, 0, 0, 45, 1.0);
    assert(moved.take());
    double tx, ty;
    tipAt(cell.positions(), tx, ty);
    assert(std::hypot(tx - kAxisX - kCamX, ty - kAxisY - kCamY) < 0.004);

    feed.stop();
    cell.disconnect();
    return 0;
}
