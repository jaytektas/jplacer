// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// A camera lost in the middle of a task (unplugged, hung, frozen on one
// picture): the feed says so and opens it again, and a task waiting for a
// picture waits for it to come back rather than failing, then carries on;
// each as the camera's settings say.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "camera/JPCameraFeed.h"
#include "tasks/JPCameraLook.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

using namespace jf;

namespace {

JPCameraConfig camera(const char* name, const char* how, int after) {
    JPCameraConfig c;
    c.name = name;
    c.device = JJson::object();
    c.device["backend"] = "simulated";
    c.device["width"] = 64;
    c.device["height"] = 48;
    c.device["fps"] = 30;
    c.device[how] = after;
    return c;
}

bool waitFor(const std::function<bool()>& what, int seconds) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    while (!what() && std::chrono::steady_clock::now() < until) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    return what();
}

} // namespace

int main() {
    // Hung: no pictures. Lost, and while lost a picture asked for is waited
    // for, and taken once it is back.
    {
        JPCameraFeed feed(camera("Hanging", "hangAfterFrames", 5));
        feed.start();
        assert(waitFor([&] { return feed.isLost(); }, 8));
        assert(feed.lostWhy().find("no picture") != std::string::npos);
        JPFrame img;
        std::string why;
        const bool got = JPCameraLook::takenFrame(feed, img, why, 0);
        if (!got) std::fprintf(stderr, "why: %s\n", why.c_str());
        assert(got && img.width == 64 && !feed.isLost());   // back by itself (opened again), and the picture taken
        feed.stop();
    }
    // Frozen: the very same picture over and over is taken as hung too.
    {
        JPCameraFeed feed(camera("Freezing", "freezeAfterFrames", 5));
        feed.start();
        assert(waitFor([&] { return feed.isLost(); }, 8));
        assert(feed.lostWhy().find("same picture") != std::string::npos);
        JPFrame img;
        std::string why;
        assert(JPCameraLook::takenFrame(feed, img, why, 0));
        feed.stop();
    }
    // As the camera is set: hung after 1 s with no picture; not waited for at
    // all, so a picture asked for fails at once, saying the camera was lost.
    {
        JPCameraConfig c = camera("Impatient", "hangAfterFrames", 5);
        c.lost.noPictureS = 1;
        c.lost.waitS = 0;
        JPCameraFeed feed(c);
        feed.start();
        const auto started = std::chrono::steady_clock::now();
        assert(waitFor([&] { return feed.isLost(); }, 8));
        assert(std::chrono::steady_clock::now() - started < std::chrono::milliseconds(2500));
        JPFrame img;
        std::string why;
        assert(!JPCameraLook::takenFrame(feed, img, why, 0) && why.find("was lost") != std::string::npos);
        feed.stop();
    }
    // The same picture not counted: a camera frozen on one is not lost.
    {
        JPCameraConfig c = camera("Still", "freezeAfterFrames", 5);
        c.lost.samePictureS = 0;
        JPCameraFeed feed(c);
        feed.start();
        assert(!waitFor([&] { return feed.isLost(); }, 5));
        feed.stop();
    }
    std::printf("=== lost cameras are waited for ===\n");
    return 0;
}
