// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// The simulated camera draws a mark where the machine's geometry puts it, and
// the calibration pipeline finds it there: with the viewpoint a known distance
// from the mark, the mark appears at centre + M (V - P) for the hidden M.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "camera/JPCameraFeed.h"
#include "pipeline/JPDefaultPipelines.h"
#include "tasks/JPPipelineMarkFinder.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <thread>

using namespace jf;

int main() {
    const double M[4] = { -25.7, -0.11, -0.11, 25.6 };   // looking down, turned a quarter degree
    JPCameraConfig cam;
    cam.name = "Top";
    cam.device = JJson::parse(R"({ "backend": "simulated", "width": 640, "height": 480, "fps": 30,
        "scene": { "pxPerMm": [-25.7, -0.11, -0.11, 25.6], "ground": 30, "mark": 190, "noise": 3,
                   "marks": [ { "x": 137.137, "y": 179.265, "diameter": 1.85 } ] } })");
    const double vx = 137.137 + 0.8, vy = 179.265 - 0.5;
    JPCameraFeed feed(cam);
    feed.setView([&](double& x, double& y) { x = vx; y = vy; return true; });
    std::atomic<int> frames{ 0 };
    feed.onFrame.connect([&](uint64_t) { ++frames; });
    feed.start();
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (frames < 2 && std::chrono::steady_clock::now() < until) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    JPFrame f;
    assert(feed.latest(f, 0));
    feed.stop();

    const double dx = vx - 137.137, dy = vy - 179.265;
    const double ex = 320 + M[0] * dx + M[1] * dy, ey = 240 + M[2] * dx + M[3] * dy;
    // Found as the camera's calibration finds it: by OpenPnP's Advanced Calibration pipeline.
    JPPipelineMarkFinder finder(JPDefaultPipelines::cameraCalibration());
    const JPRoundMark m = finder.find(f, 320, 240, 60, 1.85 * 25.65);
    assert(m.found);
    // Where the scene drew it, to within the pipeline's step (an eighth of a pixel) and the noise.
    assert(std::abs(m.x - ex) < 0.2 && std::abs(m.y - ey) < 0.2);
    return 0;
}
