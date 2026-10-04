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
#include "library/JPFootprintMaker.h"
#include "library/JPStarterLibrary.h"
#include "tasks/JPRotationLook.h"

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
        p.reference = true;
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
    JPPlacement u;
    u.designator = "U5";
    u.x = 60;
    u.y = 30;
    u.rotationDeg = 90;
    u.side = S::Bottom;
    u.footprint = "SOIC-8";
    b.placements.push_back(u);
    JPPlacement q;
    q.designator = "Q7";
    q.x = 100;
    q.y = 40;
    q.rotationDeg = 0;   // drawn a quarter turn round from this: its package's 0° is out
    q.side = S::Bottom;
    q.footprint = "SOT-23";
    b.placements.push_back(q);
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

// U5's SOIC-8 pads where the hidden board puts them: each corner from the
// footprint's own mm, mirrored (bottom side), turned, onto the board, onto the machine.
JPFootprint soic8() {
    JPFootprintMaker::Dual d;
    return JPFootprintMaker::dual("SOIC-8", d);
}

JPFootprint sot23() {
    JPPartsStore s;
    JPStarterLibrary::fill(s);
    return *s.footprint(s.packageNamed("SOT-23")->footprintId);
}

// A placement's pads, as JSON shapes on the machine: mirrored (bottom side),
// turned by its rotation and `extra` degrees, onto the board and the machine.
std::string padsOf(const JPPlacement& u, const JPFootprint& f, double extra) {
    const JPAffine2D h = hidden();
    const double a = (u.rotationDeg + extra) * M_PI / 180;
    std::string out;
    for (const JPPad& p : f.pads) {
        std::string pts;
        for (const auto& [cx, cy] : { std::pair{ -1, -1 }, std::pair{ 1, -1 }, std::pair{ 1, 1 }, std::pair{ -1, 1 } }) {
            const double lx = -(p.x + cx * p.width / 2), ly = p.y + cy * p.height / 2;   // mirrored
            const double bx = u.x + lx * std::cos(a) - ly * std::sin(a), by = u.y + lx * std::sin(a) + ly * std::cos(a);
            double mx, my;
            h.apply(bx, by, mx, my);
            char buf[64];
            std::snprintf(buf, sizeof buf, "%s[%.6f, %.6f]", pts.empty() ? "" : ", ", mx, my);
            pts += buf;
        }
        out += (out.empty() ? "" : ", ") + std::string("{ \"points\": [") + pts + "] }";
    }
    return out;
}

std::string pads(const JPBoard& b) {
    return padsOf(*b.find("U5"), soic8(), 0) + ", " + padsOf(*b.find("Q7"), sot23(), 90);
}

JPCellConfig cellConfig(const std::string& marks, const std::string& shapes) {
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
                                  "marks": [ )" + marks + R"( ], "shapes": [ )" + shapes + R"( ] } } } ]
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
    JPCell cell(cellConfig(scene(b), pads(b)), profiles());
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
    assert(r.ok && r.affine && r.references.size() == 5 && r.rmsMm < 0.005);
    for (const auto& f : r.references) assert(f.found && f.residualMm < 0.01);
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

    // Looked at from either side (parallax), off the middle of the picture:
    // the same board, as closely.
    JPBoardLocator::Options sides = o;
    sides.fiducials.parallaxDiameterMm = 2;
    sides.fiducials.parallaxAngleDeg = 30;
    sides.fiducials.passes = 3;
    const JPBoardLocator::Result parallax = JPBoardLocator::run(cell, feed, b, guess, sides);
    if (!parallax.ok) std::fprintf(stderr, "why: %s\n", parallax.why.c_str());
    assert(parallax.ok && parallax.rmsMm < 0.005);
    for (const JPPlacement& p : b.placements) {
        if (p.side != JPPlacement::Side::Bottom) continue;
        double ax, ay, bx, by;
        squared.board.toMachine.apply(p.x, p.y, ax, ay);
        parallax.board.toMachine.apply(p.x, p.y, bx, by);
        assert(std::hypot(ax - bx, ay - by) < 0.005);
    }

    // The top side has one fiducial: not enough.
    const JPBoardLocator::Result top = JPBoardLocator::run(cell, feed, b, JPBoardSide::placed(JPPlacement::Side::Top, 180, 70, 0), o);
    assert(!top.ok && top.why.find("two are needed") != std::string::npos);
    // A guess far out: the fiducials are not where it says.
    const JPBoardLocator::Result lost = JPBoardLocator::run(cell, feed, b, JPBoardSide::placed(JPPlacement::Side::Bottom, 320, 40, 0), o);
    assert(!lost.ok && !lost.why.empty());

    // A part as a reference, found by its pads drawn from its footprint.
    {
        JPBoard withPart = b;
        withPart.find("U5")->reference = true;
        JPBoardLocator::Options byPads = o;
        byPads.footprintOf = [](const JPPlacement& p, JPFootprint& f, double& degrees) {
            if (p.footprint != "SOIC-8") return false;
            f = soic8();
            degrees = p.rotationDeg;
            return true;
        };
        const JPBoardLocator::Result part = JPBoardLocator::run(cell, feed, withPart, guess, byPads);
        if (!part.ok) std::fprintf(stderr, "why: %s\n", part.why.c_str());
        assert(part.ok && part.references.size() == 6);
        for (const auto& f : part.references) {
            if (f.designator != "U5") continue;
            if (!f.found) std::fprintf(stderr, "U5: %s\n", f.why.c_str());
            assert(f.found && f.residualMm < 0.02);
        }
    }

    // Which way round: Q7's SOT-23 is a quarter turn round from its rotation;
    // U5's SOIC looks the same turned half round, so no angle wins.
    {
        const JPRotationLook::Result q7 = JPRotationLook::run(cell, feed, r.board, *b.find("Q7"), sot23(), 0, 1.0);
        if (!q7.ok) std::fprintf(stderr, "Q7: %s\n", q7.why.c_str());
        assert(q7.ok && q7.quarters == 1);
        const JPRotationLook::Result u5 = JPRotationLook::run(cell, feed, r.board, *b.find("U5"), soic8(), 90, 1.0);
        assert(!u5.ok && u5.why.find("no angle") != std::string::npos);
    }

    // A part marked as a reference that the camera has no way to find: left out, and said why.
    {
        JPBoard withPart = b;
        withPart.find("R1")->reference = true;
        const JPBoardLocator::Result part = JPBoardLocator::run(cell, feed, withPart, guess, o);
        assert(part.ok && part.references.size() == 6);
        for (const auto& f : part.references)
            if (f.designator == "R1") assert(!f.found && f.why.find("record it by hand") != std::string::npos);
    }

    // Recorded by hand: the board fitted to the positions recorded, no camera.
    {
        JPBoard recorded = b;
        for (JPPlacement& p : recorded.placements) {
            if (!p.reference || p.side != JPPlacement::Side::Bottom) continue;
            p.recorded = true;
            h.apply(p.x, p.y, p.recordedX, p.recordedY);
        }
        recorded.find("FID12")->recorded = false;
        const JPBoardLocator::Result byHand = JPBoardLocator::fitRecorded(recorded, guess, o);
        assert(byHand.ok && byHand.affine && byHand.rmsMm < 1e-6 && byHand.references.size() == 5);
        for (const auto& f : byHand.references)
            if (f.designator == "FID12") assert(!f.found && f.why == "not recorded yet");
        double ex, ey, gx, gy;
        h.apply(40.5, 80.25, ex, ey);
        byHand.board.toMachine.apply(40.5, 80.25, gx, gy);
        assert(std::hypot(gx - ex, gy - ey) < 1e-6);
        // One recorded: not enough.
        for (JPPlacement& p : recorded.placements) p.recorded = p.designator == "FID3";
        assert(!JPBoardLocator::fitRecorded(recorded, guess, o).ok);
    }

    feed.stop();
    cell.disconnect();
    return 0;
}
