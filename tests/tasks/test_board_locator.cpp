// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// Finding a board by its fiducials, with no hardware: a simulated board,
// bottom side up, turned and moved, on a machine whose Y is not square to its
// X, with a hole and a bigger pad beside two fiducials. From a guess a couple
// of millimetres and half a degree out, the locator must find every fiducial
// and place the board to a few micrometres, skew and all.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "tasks/JPBoardLocator.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <mutex>

using namespace jf;

namespace {

constexpr double kM[4] = { -25.97, 0.035, 0.007, 25.96 };   // as bench's top camera

JPBoard board() {
    JPBoard b;
    b.name = "Test";
    auto fid = [&b](const char* d, double x, double y, JPPlacement::Side s) {
        JPPlacement p;
        p.designator = d;
        p.x = x;
        p.y = y;
        p.side = s;
        p.fiducial = true;
        p.footprint = "FIDUCIAL_1MM";
        b.placements.push_back(p);
    };
    using S = JPPlacement::Side;
    fid("FID1", 2.383, 9.535, S::Top);
    fid("FID3", 137.393, 2.814, S::Bottom);
    fid("FID4", 79.078, 40.651, S::Bottom);
    fid("FID7", 161.738, 101.969, S::Bottom);
    fid("FID8", 109.329, 78.052, S::Bottom);
    fid("FID12", 2.791, 18.734, S::Bottom);
    JPPlacement r;
    r.designator = "R1";
    r.x = 40.5;
    r.y = 80.25;
    r.side = S::Bottom;
    b.placements.push_back(r);
    return b;
}

// Where the board really is: bottom up, turned, moved, on a skewed machine.
JPAffine2D hidden() {
    JPAffine2D skew;
    skew.b = 0.0025;
    return skew.after(JPBoardSide::placed(JPPlacement::Side::Bottom, 347.939, 69.282, -0.6).toMachine);
}

std::string scene(const JPBoard& b) {
    const JPAffine2D h = hidden();
    std::string marks;
    for (const JPPlacement* p : b.fiducials(JPPlacement::Side::Bottom)) {
        double x, y;
        h.apply(p->x, p->y, x, y);
        char buf[160];
        std::snprintf(buf, sizeof buf, "%s{ \"x\": %.6f, \"y\": %.6f, \"diameter\": 1.0 }", marks.empty() ? "" : ", ", x, y);
        marks += buf;
        // Beside two of them, a hole and a bigger pad, as round as any fiducial.
        if (p->designator == "FID4") {
            std::snprintf(buf, sizeof buf, ", { \"x\": %.6f, \"y\": %.6f, \"diameter\": 1.0, \"level\": 5 }", x + 2.5, y + 1.0);
            marks += buf;
        }
        if (p->designator == "FID8") {
            std::snprintf(buf, sizeof buf, ", { \"x\": %.6f, \"y\": %.6f, \"diameter\": 2.0 }", x - 3.0, y);
            marks += buf;
        }
    }
    return marks;
}

JPCellConfig cellConfig(const std::string& marks) {
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
      "cameras": [ { "id": "C", "name": "Top", "mount": { "head": "H", "axisX": "X", "axisY": "Y" },
                     "device": { "backend": "simulated", "width": 640, "height": 480, "fps": 60,
                       "scene": { "pxPerMm": [-25.97, 0.035, 0.007, 25.96], "ground": 60, "mark": 200, "noise": 3,
                                  "marks": [ )" + marks + R"( ] } } } ]
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
    const JPBoard b = board();
    JPCell cell(cellConfig(scene(b)), profiles());
    Latch connected, moved;
    cell.onConnection.connect([&](bool ok, std::string) { connected.set(ok); });
    cell.onMotion.connect([&](bool ok, std::string) { moved.set(ok); });
    cell.connect();
    assert(connected.take());
    cell.home();
    assert(moved.take());

    // The scene is drawn in the axes' own coordinates: with a squareness
    // correction set, the camera is where the axes are, not where the square
    // coordinates say.
    std::atomic<double> lean{ 0 };
    JPCameraFeed feed(cell.config().cameras.front());
    feed.setView([&](double& x, double& y) {
        const auto p = cell.positions();
        x = p.at("X") - lean * p.at("Y");
        y = p.at("Y");
        return true;
    });
    feed.start();
    // Calibrated as the scene draws (the calibrator has its own test).
    JPCameraCalibration cal;
    cal.valid = true;
    cal.pxPerMm = { kM[0], kM[1], kM[2], kM[3] };
    cal.width = 640;
    cal.height = 480;
    cell.setCameraCalibration("C", cal);

    JPBoardLocator::Options o;
    o.speed = 1.0;
    // A guess a few millimetres and two degrees out (a board put down by hand,
    // the camera put on one fiducial by eye): the furthest fiducial is then
    // several millimetres from where the guess puts it.
    const JPBoardSide guess = JPBoardSide::placed(JPPlacement::Side::Bottom, 345.4, 72.3, 1.4);
    const JPBoardLocator::Result r = JPBoardLocator::run(cell, feed, b, guess, o);
    if (!r.ok) std::fprintf(stderr, "why: %s\n", r.why.c_str());
    assert(r.ok && r.affine && r.fiducials.size() == 5 && r.rmsMm < 0.005);
    for (const auto& f : r.fiducials) assert(f.found && f.residualMm < 0.01);
    // Every placement where the board really puts it, the part as well as the fiducials.
    const JPAffine2D h = hidden();
    for (const JPPlacement& p : b.placements) {
        if (p.side != JPPlacement::Side::Bottom) continue;
        double ex, ey, gx, gy;
        h.apply(p.x, p.y, ex, ey);
        r.board.toMachine.apply(p.x, p.y, gx, gy);
        assert(std::hypot(gx - ex, gy - ey) < 0.005);
    }
    assert(std::abs(r.board.toMachine.rotationDeg() + 0.6) < 0.01 && r.board.toMachine.mirrored());
    // The board is square, so the lean it shows is the machine's: square X =
    // axis X + xPerY axis Y undoes a machine that carries X along with Y.
    assert(std::abs(r.xPerY + 0.0025) < 1e-4);

    // Squared by what it measured, and homed again: the same board is now found square.
    JPSquarenessConfig square;
    square.axisX = "X";
    square.axisY = "Y";
    square.xPerY = r.xPerY;
    cell.setSquareness(square);
    lean = r.xPerY;
    cell.home();
    assert(moved.take());
    const JPBoardLocator::Result squared = JPBoardLocator::run(cell, feed, b, guess, o);
    assert(squared.ok && std::abs(squared.xPerY) < 1e-4 && squared.rmsMm < 0.005);

    // The top side has one fiducial: not enough.
    const JPBoardLocator::Result top = JPBoardLocator::run(cell, feed, b, JPBoardSide::placed(JPPlacement::Side::Top, 180, 70, 0), o);
    assert(!top.ok && top.why.find("two are needed") != std::string::npos);
    // A guess far out: the fiducials are not where it says.
    const JPBoardLocator::Result lost = JPBoardLocator::run(cell, feed, b, JPBoardSide::placed(JPPlacement::Side::Bottom, 320, 40, 0), o);
    assert(!lost.ok && !lost.why.empty());

    feed.stop();
    cell.disconnect();
    return 0;
}
