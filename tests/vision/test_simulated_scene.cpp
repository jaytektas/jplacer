// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// The simulated camera draws a mark where the machine's geometry puts it, and
// the round-mark finder finds it there: with the viewpoint a known distance
// from the mark, the mark appears at centre + M (V - P) for the hidden M.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "camera/JPCameraFeed.h"
#include "vision/JPRoundMarkFinder.h"

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
    JPRoundMarkFinder::Request rq;
    rq.expectedX = 320;
    rq.expectedY = 240;
    rq.searchRadius = 60;
    rq.diameter = 1.85 * 25.65;
    const JPRoundMark m = JPRoundMarkFinder::find(JPGrayImage::fromRgba(f.rgba.data(), f.width, f.height), rq);
    assert(m.found);
    assert(std::abs(m.x - ex) < 0.1 && std::abs(m.y - ey) < 0.1);
    return 0;
}
