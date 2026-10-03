// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// Measuring an axis's play with the head camera, with no hardware: the
// simulated camera rides an X axis with 0.08 mm of play (where the drive is
// sent and where the camera is differ by up to the play, depending on which
// way it last moved). The calibrator must find the play, choose Directional
// compensation (the play is the same at every speed), and, so compensated,
// come in to the mark from anywhere to within the tolerance.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "tasks/JPBacklashCalibrator.h"

#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <mutex>

using namespace jf;

namespace {

constexpr double kPlay = 0.08;
constexpr double kM[4] = { -25.97, 0.035, 0.007, 25.96 };

JPCellConfig cellConfig() {
    const std::string json = R"({
      "name": "Test",
      "drivers": [ { "id": "D", "name": "Gantry", "statusIntervalMs": 10, "commandTimeoutMs": 500, "connectWaitMs": 0,
                     "link": { "type": "simulated", "simulator": {
                         "identity": [ "[VER:1.1f.20250101:]", "[FIRMWARE:grblHAL]" ], "axisLetters": [ "X", "Y" ] } } } ],
      "heads": [ { "id": "H", "name": "Head" } ],
      "axes": [ { "id": "X", "name": "x", "kind": "controller", "type": "x", "driver": "D", "letter": "X",
                  "homeCoordinate": 120, "feedratePerSecond": 2000 },
                { "id": "Y", "name": "y", "kind": "controller", "type": "y", "driver": "D", "letter": "Y",
                  "homeCoordinate": 120, "feedratePerSecond": 2000 } ],
      "cameras": [ { "id": "C", "name": "Top", "mount": { "head": "H", "axisX": "X", "axisY": "Y" },
                     "device": { "backend": "simulated", "width": 640, "height": 480, "fps": 60,
                       "scene": { "pxPerMm": [-25.97, 0.035, 0.007, 25.96], "ground": 60, "mark": 200, "noise": 3,
                                  "marks": [ { "x": 100, "y": 100, "diameter": 1.0 } ] } } } ]
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
    Latch connected, moved;
    cell.onConnection.connect([&](bool ok, std::string) { connected.set(ok); });
    cell.onMotion.connect([&](bool ok, std::string) { moved.set(ok); });
    cell.connect();
    assert(connected.take());
    cell.home();
    assert(moved.take());

    // The camera lags the drive by up to the play: it follows only once the
    // drive has taken up the slack the way it is going.
    // Followed at every position the controller reports (each move ends
    // with one), not only at each picture: moves come quicker than pictures.
    std::mutex playMutex;
    double camera = 120, cameraY = 120;
    auto follow = cell.onPositions.connect([&](std::map<std::string, double>) {
        const auto p = cell.reportedPositions();
        const auto px = p.find("X"), py = p.find("Y");
        if (px == p.end() || py == p.end()) return;
        std::lock_guard lk(playMutex);
        if (px->second > camera + kPlay / 2) camera = px->second - kPlay / 2;
        if (px->second < camera - kPlay / 2) camera = px->second + kPlay / 2;
        cameraY = py->second;
    });
    JPCameraFeed feed(cell.config().cameras.front());
    feed.setView([&](double& x, double& y) {
        std::lock_guard lk(playMutex);
        x = camera;
        y = cameraY;
        return true;
    });
    feed.start();
    JPCameraCalibration cal;
    cal.valid = true;
    cal.pxPerMm = { kM[0], kM[1], kM[2], kM[3] };
    cal.width = 640;
    cal.height = 480;
    cell.setCameraCalibration("C", cal);

    JPBacklashCalibrator::Options o;
    o.markX = 100;
    o.markY = 100;
    o.markDiameterMm = 1.0;
    o.reachMm = 2;
    o.still = 4;
    o.tries = 4;
    const JPBacklashCalibrator::Result r = JPBacklashCalibrator::run(cell, feed, "X", o);
    if (!r.ok) std::fprintf(stderr, "why: %s\n", r.why.c_str());
    std::fprintf(stderr, "method %s, offset %.4f, tolerance %.4f, worst after %.4f\n", JPAxisConfig::backlashWord(r.method),
                 r.offset, r.data.toleranceMm, r.worstAfterMm);
    assert(r.ok);
    // The play, found to within the tolerance either way (the mark's centre
    // is found to a few hundredths of a pixel), the same at every speed.
    assert(r.method == JPAxisConfig::Backlash::Directional);
    assert(std::abs(r.offset - kPlay) <= 2 * r.data.toleranceMm);
    assert(!r.data.byDistance.empty() && r.data.bySpeed.size() == std::size(JPBacklashCalibrator::kSpeeds));
    // A short way in from the other side takes up only that much of the play.
    assert(r.data.byDistance.front().second < kPlay / 2);
    // Compensated, every move comes in to the same place: the 4 tried, and one
    // from afar on each side.
    assert(r.data.after.size() == 6 && r.worstAfterMm <= 2 * r.data.toleranceMm);
    // The axis it was measured on: not one the camera rides.
    const JPBacklashCalibrator::Result wrong = JPBacklashCalibrator::run(cell, feed, "Z", o);
    assert(!wrong.ok && !wrong.why.empty());

    feed.stop();
    follow();
    cell.disconnect();
    return 0;
}
