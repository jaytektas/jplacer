// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// OpenPnP's BlindsFeeder.FindFeatures: among rotated rectangles seen by the
// camera, the fiducials (diamonds) and the blinds; from the blinds the
// pocket centerline, size, pitch and the cover's position.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "model/JPBlindsFeeders.h"
#include "model/JPConfiguration.h"
#include "tasks/JPBlindsVision.h"
#include "vision/JPSimpleHistogram.h"

#include <cmath>
#include <filesystem>

using namespace jf;
namespace fs = std::filesystem;

namespace {
constexpr JPLengthUnit kMm = JPLengthUnit::Millimeters;
constexpr double kPx = 20;   // px per mm; Y up on the machine, down in the picture
bool near(double a, double b, double tol) { return std::abs(a - b) < tol; }
}

int main() {
    // The histogram: the commonest value, spread to its neighbours.
    JPSimpleHistogram h(0.1);
    for (double v : { 1.0, 1.02, 0.98, 3.0 }) h.add(v, 1);
    assert(near(h.maximumKey(), 1.0, 1e-9));
    assert(std::isnan(JPSimpleHistogram(1).maximumKey()));

    const fs::path dir = fs::temp_directory_path() / "jplacer-test-blinds-vision";
    fs::create_directories(dir);
    JPConfiguration c(dir.string());
    JPFeeder f = JPFeeder::create("org.openpnp.machine.reference.feeder.BlindsFeeder", "R1");
    const std::string id = f.id();
    c.addFeeder(std::move(f));
    // The holder at (1, 1) (a fiducial at the origin is one not set).
    JPBlindsFeeders::setFiducial(c, id, 1, JPLocation(kMm, 1, 1, 0, 0));
    JPBlindsFeeders::setFiducial(c, id, 2, JPLocation(kMm, 81, 1, 0, 0));
    JPBlindsFeeders::setFiducial(c, id, 3, JPLocation(kMm, 1, 41, 0, 0));
    const JPFeeder& feeder = *c.feeder(id);

    // The camera over (20, 10) on the holder, (21, 11) on the machine: 640 x 480.
    JPJobMachine::Sight s;
    s.at = JPLocation(kMm, 21, 11, 0, 0);
    s.mmPerPixelX = s.mmPerPixelY = 1 / kPx;
    s.width = 640;
    s.height = 480;
    s.toMachine = [](double px, double py) { return JPLocation(kMm, 21 + (px - 320) / kPx, 11 - (py - 240) / kPx, 0, 0); };
    s.toPixel = [](const JPLocation& l, double& px, double& py) {
        px = 320 + (l.x() - 21) * kPx;
        py = 240 - (l.y() - 11) * kPx;
        return true;
    };
    // A place on the holder, in the picture.
    auto pixel = [&](double x, double y) {
        double px = 0, py = 0;
        s.toPixel(JPLocation(kMm, x + 1, y + 1, 0, 0), px, py);
        return cv::Point2f(float(px), float(py));
    };
    // Blinds 2 x 1.6 mm, every 4 mm from 14 mm, across the tape at 10 mm; a fiducial diamond near the camera.
    std::vector<cv::RotatedRect> rects;
    for (int i = 0; i < 4; ++i) rects.emplace_back(pixel(14 + 4 * i, 10), cv::Size2f(2 * kPx, 1.6f * kPx), 0.f);
    rects.emplace_back(pixel(21, 11), cv::Size2f(1.8f * kPx, 1.8f * kPx), -45.f);
    JPBlindsVision::Found found;
    JPBlindsVision::find(feeder, rects, s, found);
    assert(found.fiducials.size() == 1);
    assert(found.blinds.size() == 4);
    // Along the tape, nearest first.
    assert(s.toMachine(found.blinds.front().center.x, found.blinds.front().center.y).x() < 16);
    assert(near(found.pocketCenterlineMm, 10, 1e-6) && near(found.pocketSizeMm, 1.6, 0.11));
    assert(near(found.pocketPitchMm, 4, 1e-6));
    assert(near(found.pocketPositionMm, 2, 0.06));
    // Drawn without trouble.
    cv::Mat picture(480, 640, CV_8UC3, cv::Scalar(0, 0, 0));
    JPBlindsVision::draw(picture, feeder, found, s);
    return 0;
}
