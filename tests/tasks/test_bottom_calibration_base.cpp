// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// A fixed camera looking up, calibrated by a nozzle's tip carried over it, the nozzle's base round the tip: a
// dimmer disc three times its size, as a real nozzle's is. With the tip's size given (as jplacer gives it, the
// tip's Vision Diameter or Diameter), the tip is found and measured, never the base, though the base is the
// bigger, plainer round thing; at both heights (the second pass started from the first's scale). The scale and
// the transform come out right, and the tip measures its own size.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "tasks/JPCameraCalibrator.h"

#include "vision/JPRoundMarkFinder.h"

#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <vector>

using namespace jf;

namespace {

constexpr double kM[4] = { 30.1, 0.12, 0.08, 30.0 };   // looking up: mirrored against the head camera's
constexpr double kCamX = 60, kCamY = 95;               // where the camera is
constexpr double kOffX = 10, kOffY = -5;               // the nozzle on the head

JPCellConfig cellConfig() {
    char camera[512];
    std::snprintf(camera, sizeof camera, R"({ "id": "B", "name": "Bottom", "looking": "up",
        "mount": { "offset": { "x": %g, "y": %g, "z": -24 } }, "unitsPerPixel": { "x": 0.04, "y": 0.04 },
        "device": { "backend": "simulated", "width": 640, "height": 480, "fps": 60,
          "scene": { "pxPerMm": [30.1, 0.12, 0.08, 30.0], "ground": 20, "mark": 180, "noise": 3,
                     "marks": [ { "x": %g, "y": %g, "diameter": 3.6, "level": 110 },
                                { "x": %g, "y": %g, "diameter": 1.2 } ] } } })", kCamX, kCamY, kCamX, kCamY, kCamX, kCamY);
    const std::string json = R"({
      "name": "Test",
      "drivers": [ { "id": "D", "name": "Gantry", "statusIntervalMs": 10, "commandTimeoutMs": 500, "connectWaitMs": 0,
                     "link": { "type": "simulated", "simulator": {
                         "identity": [ "[VER:1.1f.20250101:]", "[FIRMWARE:grblHAL]" ], "axisLetters": [ "X", "Y" ] } } } ],
      "heads": [ { "id": "H", "name": "Head" } ],
      "axes": [ { "id": "X", "name": "x", "kind": "controller", "type": "x", "driver": "D", "letter": "X",
                  "homeCoordinate": 390, "feedratePerSecond": 2000 },
                { "id": "Y", "name": "y", "kind": "controller", "type": "y", "driver": "D", "letter": "Y",
                  "homeCoordinate": 444, "feedratePerSecond": 2000 } ],
      "nozzles": [ { "id": "N", "name": "Left", "mount": { "head": "H", "axisX": "X", "axisY": "Y",
                                                          "offset": { "x": 10, "y": -5, "z": 0 } } } ],
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
    cell.home();
    assert(homed.take());

    // The camera stays; the tip is where the nozzle is. Drawn as a camera at
    // 2C - N looking at a mark at C: the tip appears at mid + M (C - N).
    const JPCameraConfig& cam = cell.config().cameras.front();
    const JPNozzleConfig& nozzle = cell.config().nozzles.front();
    JPCameraFeed feed(cam);
    feed.setView([&](double& x, double& y) {
        const auto p = cell.positions();
        x = 2 * kCamX - (p.at("X") + kOffX);
        y = 2 * kCamY - (p.at("Y") + kOffY);
        return true;
    });
    feed.start();

    // The tip roughly over the camera.
    std::string why;
    assert(cell.moveAxesAndWait({ { "X", kCamX - kOffX + 0.3 }, { "Y", kCamY - kOffY - 0.2 } }, 1.0, why));
    JPCameraCalibrator::Options o;
    o.speed = 1.0;
    o.moving = &nozzle.mount;
    o.markDiameterMm = 1.2;   // the tip's; the base round it 3.6
    std::vector<double> sizes;
    o.found = [&](const JPGrayImage&, double, double, double diameterPx, const std::string&) { sizes.push_back(diameterPx); };
    const auto cal = JPCameraCalibrator::run(cell, feed, o, why);
    if (!cal) std::fprintf(stderr, "why: %s\n", why.c_str());
    // Every find the tip's size (1.2 mm at 30 px/mm), not the base's (108 px).
    assert(!sizes.empty());
    for (double d : sizes) assert(std::abs(d - 36) < 6);
    // A second pass at the first's scale finds the tip too.
    JPCameraCalibrator::Options o2 = o;
    o2.markPxPerMm = cal ? cal->scale() : 0;
    sizes.clear();
    const auto again = JPCameraCalibrator::run(cell, feed, o2, why);
    if (!again) std::fprintf(stderr, "why: %s\n", why.c_str());
    assert(again && !sizes.empty());
    for (double d : sizes) assert(std::abs(d - 36) < 6);
    assert(cal && cal->valid && cal->rmsPx < 0.05);
    for (int i = 0; i < 4; ++i) assert(std::abs(cal->pxPerMm[i] - kM[i]) < 0.002 * 30);
    // Looking up, it is the mirror image a straight-mounted up camera sees: not mirrored, barely turned.
    assert(!cal->mirrored(true) && std::abs(cal->rotationDeg(true)) < 0.5);

    // From the tip's pixel, with the camera's place as where it looks: where the nozzle is.
    assert(cell.moveAxesAndWait({ { "X", kCamX - kOffX + 1.1 }, { "Y", kCamY - kOffY - 0.7 } }, 1.0, why));
    JPFrame f;
    const auto from = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
    while (!(feed.latest(f, 0) && f.captured >= from)) {}
    JPRoundMarkFinder::Request rq;
    rq.expectedX = 320;
    rq.expectedY = 240;
    rq.searchRadius = 100;
    rq.diameter = 1.2 * 30;
    const JPRoundMark tip = JPRoundMarkFinder::find(JPGrayImage::fromRgba(f.rgba.data(), f.width, f.height), rq);
    assert(tip.found);
    double x, y;
    assert(cal->machinePoint(tip.x, tip.y, kCamX, kCamY, x, y));
    assert(std::abs(x - (kCamX + 1.1)) < 0.005 && std::abs(y - (kCamY - 0.7)) < 0.005);

    feed.stop();
    cell.disconnect();
    return 0;
}
