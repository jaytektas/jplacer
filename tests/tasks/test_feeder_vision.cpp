// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// OpenPnP's FeederVisionHelper: a tape's frame from its holes, the parts a
// feed brings and where each is picked; the holes found from a part (Auto
// Setup) and calibrated from between them, the pick location moved with
// them onto EIA-481's grid, and the vision offset that gives; the
// calibration statistics; the features drawn.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "model/JPFeederTape.h"
#include "tasks/JPFeederVision.h"

#include <cmath>

using namespace jf;

namespace {

constexpr JPLengthUnit kMm = JPLengthUnit::Millimeters;
// The camera: 20 px/mm, 640 x 480, Y up on the machine and down in the picture.
constexpr double kPx = 20;

bool near(double a, double b, double tol = 1e-6) { return std::abs(a - b) < tol; }

JPJobMachine::Sight sightAt(double x, double y) {
    JPJobMachine::Sight s;
    s.at = JPLocation(kMm, x, y, 0, 0);
    s.mmPerPixelX = s.mmPerPixelY = 1 / kPx;
    s.width = 640;
    s.height = 480;
    s.toMachine = [x, y](double px, double py) { return JPLocation(kMm, x + (px - 320) / kPx, y - (py - 240) / kPx, 0, 0); };
    s.toPixel = [x, y](const JPLocation& l, double& px, double& py) {
        px = 320 + (l.x() - x) * kPx;
        py = 240 - (l.y() - y) * kPx;
        return true;
    };
    return s;
}

// Holes along y = `holeY`, 4 mm apart from `x0`, seen from the camera; and a pocket's circle at the camera.
JPPipelineModel holes(const JPJobMachine::Sight& s, double x0, double holeY, double dy = 0) {
    std::vector<JPPipelineModel::Circle> c;
    for (int i = 0; i < 6; ++i) {
        double px = 0, py = 0;
        s.toPixel(JPLocation(kMm, x0 + 4 * i, holeY + dy, 0, 0), px, py);
        c.push_back({ px, py, 1.5 * kPx });
    }
    c.push_back({ 320, 240, 2.5 * kPx });   // the pocket: too big for a hole
    JPPipelineModel m;
    m.value = c;
    return m;
}

} // namespace

int main() {
    // The parts a feed brings: 2 mm parts on a 4 mm feed, two; 8 mm parts, one (two feeds).
    JPFeederTape::Params p;
    p.partPitch = JPLength(2, kMm);
    assert(JPFeederTape::partsPerFeedCycle(p) == 2);
    p.partPitch = JPLength(8, kMm);
    assert(JPFeederTape::partsPerFeedCycle(p) == 1);
    // The frame: holes running along +Y turn it 90°; part 1 of 2 a pitch back along the tape.
    p.partPitch = JPLength(2, kMm);
    p.partLocation = JPLocation(kMm, 50, 60, -10, 0);
    p.hole1Location = JPLocation(kMm, 53.5, 58, 0, 0);
    p.hole2Location = JPLocation(kMm, 53.5, 62, 0, 0);
    const JPLocation last = JPFeederTape::partLocation(2, std::nullopt, p, 0);
    assert(near(last.x(), 50) && near(last.y(), 60) && near(last.z(), -10) && near(last.rotation(), 90));
    const JPLocation first = JPFeederTape::partLocation(1, std::nullopt, p, 90);
    assert(near(first.x(), 50) && near(first.y(), 62) && near(first.rotation(), 180));
    // A vision offset moves them.
    const JPLocation moved = JPFeederTape::partLocation(2, JPLocation(kMm, 0.5, -0.25, 0, 0), p, 0);
    assert(near(moved.x(), 49.5) && near(moved.y(), 60.25));
    assert(near(JPFeederTape::machineToFeeder(first, std::nullopt, p).x(), 2));

    // Auto Setup from a part at (10, 10), the holes 3.5 mm above: the nearest two, hole 1 on the left (clockwise of
    // hole 2 seen from the part), the tape along +X, the pick location the camera's, at the old Z.
    JPFeederVision::Settings settings;
    settings.tape.partPitch = JPLength(4, kMm);
    settings.tape.partLocation = JPLocation(kMm, 0, 0, -12, 0);
    JPJobMachine::Sight s = sightAt(10, 10);
    JPFeederVision::Found f;
    std::string why;
    assert(JPFeederVision::find(holes(s, 0, 13.5), JPFeederVision::Mode::FromPickLocationGetHoles, settings, s, f, why));
    assert(f.holes.size() == 6 && f.hole1 && f.hole2 && f.pick && f.visionOffset);
    assert(near(f.hole1->x(), 8, 1e-3) && near(f.hole1->y(), 13.5, 1e-3) && near(f.hole2->x(), 12, 1e-3));
    assert(near(f.pick->x(), 10) && near(f.pick->y(), 10) && near(f.pick->z(), -12) && near(f.pick->rotation(), 0, 1e-3));
    // Holes too far off the line of a tape, or none: no line.
    JPPipelineModel none;
    none.value = std::vector<JPPipelineModel::Circle> {};
    assert(!JPFeederVision::find(none, JPFeederVision::Mode::FromPickLocationGetHoles, settings, s, f, why));
    assert(why == "No line of sprocket holes can be recognized");

    // Calibrated from between the holes, the tape found 0.3 mm right and 0.2 mm down: the holes a pitch apart about
    // their middle, the pick location moved with hole 1 (on EIA-481's grid), and the vision offset that gives.
    settings.tape.partLocation = JPLocation(kMm, 10, 10, -12, 0);
    settings.tape.hole1Location = JPLocation(kMm, 8, 13.5, 0, 0);
    settings.tape.hole2Location = JPLocation(kMm, 12, 13.5, 0, 0);
    s = sightAt(10, 13.5);
    JPPipelineModel shifted = holes(s, 0.3, 13.5, -0.2);
    assert(JPFeederVision::find(shifted, JPFeederVision::Mode::CalibrateHoles, settings, s, f, why));
    assert(near(f.hole1->x(), 8.3, 1e-3) && near(f.hole1->y(), 13.3, 1e-3) && near(f.hole2->x(), 12.3, 1e-3));
    assert(near(f.pick->x(), 10.3, 1e-3) && near(f.pick->y(), 9.8, 1e-3) && near(f.pick->z(), -12));
    assert(near(f.visionOffset->x(), -0.3, 1e-3) && near(f.visionOffset->y(), 0.2, 1e-3) && f.visionOffset->z() == 0);
    // The ticks across and along the tape at the pick location are drawn as lines too.
    assert(f.lines.size() == 3);
    // Holes not where they are set: not recognized.
    settings.tape.hole1Location = JPLocation(kMm, 28, 13.5, 0, 0);
    settings.tape.hole2Location = JPLocation(kMm, 32, 13.5, 0, 0);
    assert(!JPFeederVision::find(shifted, JPFeederVision::Mode::CalibrateHoles, settings, s, f, why));
    assert(why == "The two reference sprocket holes cannot be recognized");

    // The features drawn: green holes; nothing found, crossed out in red.
    settings.tape.hole1Location = JPLocation(kMm, 8, 13.5, 0, 0);
    settings.tape.hole2Location = JPLocation(kMm, 12, 13.5, 0, 0);
    assert(JPFeederVision::find(shifted, JPFeederVision::Mode::CalibrateHoles, settings, s, f, why));
    cv::Mat picture(480, 640, CV_8UC3, cv::Scalar(0, 0, 0));
    JPFeederVision::draw(picture, f, settings, s);
    double hx = 0, hy = 0;
    s.toPixel(*f.hole1, hx, hy);
    const cv::Vec3b ring = picture.at<cv::Vec3b>(int(std::lround(hy - 15)), int(std::lround(hx)));
    assert(ring[1] > 100 && ring[2] < 100);
    cv::Mat blank(480, 640, CV_8UC3, cv::Scalar(0, 0, 0));
    JPFeederVision::draw(blank, JPFeederVision::Found {}, settings, s);
    assert(blank.at<cv::Vec3b>(240, 320)[2] > 100);

    // The statistics: an error of 0.1 then 0.3 mm; their average, and a confidence limit too wide for 0.1 mm wanted.
    JPFeeder feeder = JPFeeder::create("org.openpnp.machine.pandaplacer.BambooFeederAutoVision", "R1");
    JPFeederTape::addCalibrationError(feeder, JPLength(0.1, kMm));
    assert(!JPFeederTape::precisionSufficient(feeder));
    JPFeederTape::addCalibrationError(feeder, JPLength(0.3, kMm));
    assert(near(JPFeederTape::precisionAverage(feeder).value(), 0.2));
    assert(near(JPFeederTape::precisionConfidenceLimit(feeder).value(), std::sqrt(0.1 / std::sqrt(2.0)) * 1.64));
    assert(!JPFeederTape::precisionSufficient(feeder));
    feeder.setLengthOf("precision-wanted", JPLength(1, kMm));
    assert(JPFeederTape::precisionSufficient(feeder));
    feeder.visionOffset = JPLocation(kMm);
    JPFeederTape::resetCalibrationStatistics(feeder);
    assert(feeder.number("calibration-count") == 0 && !feeder.visionOffset);
    // Its pick location: the frame's; set again, its calibration forgotten.
    feeder.setLocationOf("hole-1-location", JPLocation(kMm, 8, 13.5, 0, 0));
    feeder.setLocationOf("hole-2-location", JPLocation(kMm, 12, 13.5, 0, 0));
    feeder.setLocation(JPLocation(kMm, 10, 10, -12, 0));
    feeder.visionOffset = JPLocation(kMm, -0.3, 0.2, 0, 0);
    const auto pick = feeder.pickLocation();
    assert(pick && near(pick->x(), 10.3) && near(pick->y(), 9.8));
    feeder.setLocation(JPLocation(kMm, 10, 10, -12, 0));
    assert(!feeder.visionOffset);
    // Discard Parts: to the end of the feed cycle (2 mm parts: two a feed).
    feeder.setLengthOf("part-pitch", JPLength(2, kMm));
    feeder.setNumber("feed-count", 3);
    JPFeederTape::discardParts(feeder);
    assert(feeder.number("feed-count") == 4);
    return 0;
}
