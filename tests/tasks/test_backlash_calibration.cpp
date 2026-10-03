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
#include <cstdlib>
#include <mutex>

using namespace jf;

namespace {

constexpr double kPlay = 0.08;
// A stretching drive: the camera lags where the drive is by L(s), s the
// travel since the drive last turned, from -kWound (still lagging the other
// way) to +kWound as it winds up over kWindMm.
constexpr double kWound = 0.03, kWindMm = 2.0;
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
    // Followed at every move sent (each G1's X and Y), not only at each
    // picture or report: moves come quicker than either, and a move sent as
    // two (backing off first) must be seen as two.
    std::mutex playMutex;
    double camera = 120, cameraY = 120;
    bool stretching = false;              // the second drive
    double drive = 120, sinceTurn = 10;   // where the drive was, its travel since it turned
    int way = 1;
    auto follow = cell.onTraffic.connect([&](std::string, bool out, std::string line) {
        if (!out || line.rfind("G1 ", 0) != 0) return;
        auto word = [&line](char letter, double& v) {
            const size_t at = line.find(std::string(" ") + letter);
            if (at == std::string::npos) return false;
            v = std::atof(line.c_str() + at + 2);
            return true;
        };
        double x;
        std::lock_guard lk(playMutex);
        word('Y', cameraY);
        if (!word('X', x)) return;
        if (!stretching) {
            if (x > camera + kPlay / 2) camera = x - kPlay / 2;
            if (x < camera - kPlay / 2) camera = x + kPlay / 2;
            return;
        }
        const double step = x - drive;
        if (step == 0) return;
        const int dir = step > 0 ? 1 : -1;
        sinceTurn = dir == way ? sinceTurn + std::abs(step) : std::abs(step);
        way = dir;
        drive = x;
        const double lag = -kWound + 2 * kWound * (1 - std::exp(-sinceTurn / (kWindMm / 3)));
        camera = drive - way * lag;
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
    // A drive that stretches: its play keeps growing with how far it came in,
    // never levelling off within a short sneak-up. Directional cannot make
    // that the same every time; one-sided and distance-aware are both tried,
    // and the better kept, landing moves together.
    {
        JPCellConfig plain = cell.config();
        plain.axes[0].backlash = JPAxisConfig::Backlash::None;
        std::string why;
        assert(cell.reconfigure(plain, why));
        std::lock_guard lk(playMutex);
        stretching = true;
        drive = cell.reportedPositions().at("X");
    }
    o.tries = 6;
    const JPBacklashCalibrator::Result s = JPBacklashCalibrator::run(cell, feed, "X", o);
    if (!s.ok) std::fprintf(stderr, "why: %s\n", s.why.c_str());
    std::fprintf(stderr, "stretching: method %s, offset %.4f, approach %.3f, tolerance %.4f, worst after %.4f\n",
                 JPAxisConfig::backlashWord(s.method), s.offset, s.approachMm, s.data.toleranceMm, s.worstAfterMm);
    assert(s.ok);
    assert(s.method == JPAxisConfig::Backlash::OneSided || s.method == JPAxisConfig::Backlash::DistanceAware);
    if (s.method == JPAxisConfig::Backlash::DistanceAware) {
        assert(!s.table.empty() && s.approachMm > 0);
        for (size_t i = 1; i < s.table.size(); ++i) assert(s.table[i].second >= s.table[i - 1].second);
        assert(std::abs(s.table.back().second - kWound) < 0.006);
    }
    assert(s.worstAfterMm <= 3 * s.data.toleranceMm);

    // The axis it was measured on: not one the camera rides.
    const JPBacklashCalibrator::Result wrong = JPBacklashCalibrator::run(cell, feed, "Z", o);
    assert(!wrong.ok && !wrong.why.empty());

    feed.stop();
    follow();
    cell.disconnect();
    return 0;
}
