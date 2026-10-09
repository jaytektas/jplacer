// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// Calibration and the visual test end to end, with no hardware: a cell over a
// simulated controller, and a simulated head camera that draws the homing mark
// where the head's position puts it, through a transform and lens only the
// scene knows.
// The calibrator must recover that transform from its moves, and the visual
// test must then find the mark where the scene put it, not where the head's
// settings say it is, and visual homing must correct the coordinates so it
// measures where they say.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "tasks/JPCameraCalibrator.h"
#include "tasks/JPVisualHoming.h"
#include "tasks/JPVisualTest.h"
#include "openpnp/JPXmlReader.h"
#include "pipeline/JPDefaultPipelines.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <condition_variable>
#include <mutex>
#include <thread>

using namespace jf;

namespace {

// Hidden from everything but the scene: looking down, turned a quarter degree,
// a little non-square, through a lens that pulls the edges in and is centred
// off the picture's middle (as bench's is).
constexpr double kM[4] = { -25.7, -0.11, -0.11, 25.6 };
constexpr double kLensK1 = -0.1;
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
                       "scene": { "pxPerMm": [-25.7, -0.11, -0.11, 25.6], "lensK1": -0.1, "lensK2": 0.03, "lensCentre": [336, 252], "ground": 30, "mark": 190, "noise": 3,
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
    // The camera sees the world, which stays put when visual homing corrects
    // the coordinates.
    feed.setView([&](double& x, double& y) {
        const auto p = cell.positions();
        const auto c = cell.correctionSinceHome();
        x = p.at("X") + (c.count("X") ? c.at("X") : 0);
        y = p.at("Y") + (c.count("Y") ? c.at("Y") : 0);
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
    // As OpenPnP's visual homing: the FIDUCIAL-HOME part's size and its fiducial pipeline (OpenPnP's stock one).
    JPVisualTest::Look look;
    look.diameterMm = head.homingFiducialDiameter;
    // Its Max. Distance: this scene's mark is well within 2 mm of its setting. At OpenPnP's 4 mm the stock
    // pipeline's coarse pass (a 7 px grid over the wider search) can now and then miss a mark this size for a
    // speck of the picture's noise, as OpenPnP's own would.
    look.maxDistanceMm = 2.0;
    {
        JPXmlElement root;
        std::string error;
        assert(JPXmlReader::parse(JPDefaultPipelines::fiducialLocator(), root, error));
        look.pipeline = std::make_shared<JPPipeline>(JPPipeline::fromXml(root));
    }
    // Without the part, OpenPnP's words.
    assert(JPVisualTest::run(cell, feed, head, 1.0, nullptr).why.find("FIDUCIAL-HOME") != std::string::npos);
    JPVisualTest::Result t = JPVisualTest::run(cell, feed, head, 1.0, &look);
    assert(!t.found && t.why.find("calibrate") != std::string::npos);

    // Roughly over the mark (not exactly: the first look must find it off centre).
    assert(cell.moveAxesAndWait({ { "X", kMarkX + 0.4 }, { "Y", kMarkY - 0.3 } }, 1.0, why));
    o.speed = 1.0;
    const auto cal = JPCameraCalibrator::run(cell, feed, o, why);
    if (!cal) std::fprintf(stderr, "why: %s\n", why.c_str());
    assert(cal && cal->valid);
    // Within 0.2%: off the middle the lens squashes a round mark more on its
    // outer side than its inner, so the middle of what is seen sits a little
    // inward of where the lens puts the mark's middle. A real lens does the
    // same; what must be exact is measured with the mark in the middle.
    for (int i = 0; i < 4; ++i) assert(std::abs(cal->pxPerMm[i] - kM[i]) < 0.002 * 25.7);
    assert(cal->rmsPx < 0.05);
    assert(std::abs(cal->lensK1 - kLensK1) < 0.005 && cal->width == 640 && cal->height == 480);
    assert(std::abs(cal->lensK2 - 0.03) < 0.01 && cal->leftOut == 0);
    assert(std::abs(cal->lensCentreX - 336) < 3 && std::abs(cal->lensCentreY - 252) < 3);
    // The middle looked where the head was, 0.4, -0.3 from the mark (taken as where the camera's place put it).
    if (!(cal->looked && std::abs(cal->lookedX - 0.4) < 0.01 && std::abs(cal->lookedY + 0.3) < 0.01))
        std::fprintf(stderr, "looked %g, %g\n", cal->lookedX, cal->lookedY);
    assert(cal->looked && std::abs(cal->lookedX - 0.4) < 0.01 && std::abs(cal->lookedY + 0.3) < 0.01);
    assert(JPCameraCalibration::fromJson(cal->toJson()).looked);
    // It went back where it began.
    const auto base = cell.jogBase();
    assert(std::abs(base.at("X") - (kMarkX + 0.4)) < 1e-6 && std::abs(base.at("Y") - (kMarkY - 0.3)) < 1e-6);

    // A mark of another size is not the mark it was told about.
    JPCameraCalibrator::Options wrong = o;
    wrong.markDiameterMm = 3.0;
    assert(!JPCameraCalibrator::run(cell, feed, wrong, why) && why.find("expected") != std::string::npos);

    // Calibrated, the visual test finds the mark where the scene put it.
    cell.setCameraCalibration("C", *cal);
    t = JPVisualTest::run(cell, feed, head, 1.0, &look);
    assert(t.found);
    // To the fiducial pipeline's step: its DetectCircularSymmetry's super-sampling of 8, an eighth of a pixel.
    constexpr double kStepMm = 1.0 / 8 / 25.6;
    assert(std::abs(t.markX - kMarkX) < kStepMm && std::abs(t.markY - kMarkY) < kStepMm);
    assert(std::abs(t.offsetX - (kMarkX - kSetX)) < kStepMm && std::abs(t.offsetY - (kMarkY - kSetY)) < kStepMm);

    // Visual homing corrects the coordinates by that, and then the mark
    // measures where its setting says.
    const JPVisualHoming::Result vh = JPVisualHoming::run(cell, feed, head, 1.0, &look);
    if (!vh.ok) std::fprintf(stderr, "visual homing: %s\n", vh.why.c_str());
    assert(vh.ok);
    assert(std::abs(vh.correctedX - (kMarkX - kSetX)) < kStepMm && std::abs(vh.correctedY - (kMarkY - kSetY)) < kStepMm);
    t = JPVisualTest::run(cell, feed, head, 1.0, &look);
    assert(t.found && std::hypot(t.offsetX, t.offsetY) < 2 * kStepMm);

    feed.stop();
    cell.disconnect();
    return 0;
}
