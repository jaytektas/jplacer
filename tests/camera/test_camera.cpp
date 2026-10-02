// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// Cameras without hardware: YUYV turns into the right RGBA, a mode is chosen
// the way the configuration asks (or the best on offer), and a simulated
// camera's feed delivers frames of its size and stops when told.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "camera/JPCameraFeed.h"
#include "camera/JPCaptureFactory.h"
#include "camera/JPPixels.h"

#include <j/config/Json.h>

#include <chrono>
#include <thread>

using namespace jf;

int main() {
    // YUYV: white and black pixel pairs (Y 235 / 16, no colour).
    const uint8_t yuyv[] = { 235, 128, 235, 128, 16, 128, 16, 128 };
    std::vector<uint8_t> rgba;
    JPPixels::yuyvToRgba(yuyv, 4, 1, rgba);
    assert(rgba.size() == 16);
    assert(rgba[0] == 255 && rgba[1] == 255 && rgba[2] == 255 && rgba[3] == 255);
    assert(rgba[8] == 0 && rgba[9] == 0 && rgba[10] == 0);
    std::string error;
    int w = 0, h = 0;
    assert(!JPPixels::jpegToRgba(yuyv, sizeof yuyv, w, h, rgba, error) && !error.empty());

    // Choosing a mode.
    const std::vector<JPCaptureMode> modes = { { "YUYV", 2592, 1944, 2 }, { "MJPG", 1280, 720, 30 },
                                               { "MJPG", 1920, 1080, 30 }, { "YUYV", 640, 480, 30 } };
    JJson none = JJson::object();
    assert(JPCaptureFactory::choose(modes, none)->width == 1920);          // biggest MJPG
    JJson asked = JJson::object();
    asked["format"] = "YUYV";
    asked["width"]  = 640;
    asked["height"] = 480;
    const auto chosen = JPCaptureFactory::choose(modes, asked);
    assert(chosen && chosen->format == "YUYV" && chosen->width == 640);
    assert(!JPCaptureFactory::choose({}, none));

    // A simulated camera's feed.
    JPCameraConfig cam;
    cam.name   = "Sim";
    cam.device = JJson::object();
    cam.device["backend"] = "simulated";
    cam.device["width"]   = 64;
    cam.device["height"]  = 48;
    cam.device["fps"]     = 50;
    JPCameraFeed feed(cam);
    std::atomic<int> frames{ 0 };
    feed.onFrame.connect([&](uint64_t) { ++frames; });
    feed.start();
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (frames < 3 && std::chrono::steady_clock::now() < until) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    assert(frames >= 3 && feed.isRunning());
    JPFrame f;
    assert(feed.latest(f, 0) && f.width == 64 && f.height == 48 && f.rgba.size() == 64 * 48 * 4);
    assert(!feed.latest(f, f.sequence) || f.sequence > 0);
    assert(feed.mode() && feed.mode()->width == 64);
    feed.stop();
    assert(!feed.isRunning());

    // A camera whose backend this system lacks says so.
    JPCameraConfig bad = cam;
    bad.device["backend"] = "carrier-pigeon";
    JPCameraFeed broken(bad);
    std::string why;
    std::atomic<bool> told{ false };
    broken.onError.connect([&](std::string w) { why = w; told = true; });
    broken.start();
    const auto until2 = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!told && std::chrono::steady_clock::now() < until2) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    assert(told && why.find("carrier-pigeon") != std::string::npos);
    broken.stop();
    return 0;
}
