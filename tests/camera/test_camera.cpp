// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// Cameras without hardware: YUYV turns into the right RGBA, a mode is chosen
// the way the configuration asks (or the best on offer), and a simulated
// camera's feed delivers frames of its size and stops when told; one that
// hangs is noticed and opened again, its pictures counted on unbroken.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "camera/JPCameraFeed.h"
#include "camera/JPCaptureFactory.h"
#include "camera/JPImageFile.h"
#include "camera/JPImageSource.h"
#include "camera/JPImageTransform.h"
#include "camera/JPPixels.h"

#include <j/config/Json.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <thread>

using namespace jf;

int main() {
    // OpenPnP's ImageCamera: the part of a picture of the table under the camera.
    {
        // A 100 x 100 picture, 1 mm a pixel: each pixel's red its column, green its row.
        JPFrame table;
        table.width = table.height = 100;
        for (int y = 0; y < 100; ++y)
            for (int x = 0; x < 100; ++x)
                for (uint8_t v : { uint8_t(x), uint8_t(y), uint8_t(0), uint8_t(255) }) table.rgba.push_back(v);
        const std::string path = (std::filesystem::temp_directory_path() / "jplacer-test-table.png").string();
        std::string error;
        assert(JPImageFile::writePng(path, table, error));
        JPImageSource::Settings st;
        st.path = path;
        st.width = 10;
        st.height = 10;
        st.unitsPerPixelX = st.unitsPerPixelY = 1;
        double vx = 30, vy = 20;
        JPImageSource cam("image", st, [&](double& x, double& y) { x = vx; y = vy; return true; });
        assert(cam.open(error));
        JPFrame f;
        cam.render(vx, vy, f);
        // The view's middle is machine (30, 20): picture column 30, row 100 - 20 = 80 (its rows run down).
        const size_t mid = (size_t(5) * 10 + 5) * 4;
        assert(f.width == 10 && f.rgba[mid] == 30 && f.rgba[mid + 1] == 80);
        // Up the machine's Y is up the view.
        const size_t above = (size_t(4) * 10 + 5) * 4;
        assert(f.rgba[above + 1] == 79);
        std::filesystem::remove(path);
    }
    // OpenPnP's image transforms: two stacked fields woven, then cut about the middle.
    {
        JPFrame f;
        f.width = 2;
        f.height = 4;
        for (uint8_t row : { 0, 1, 2, 3 })   // rows 0, 1 the even field; 2, 3 the odd one
            for (int i = 0; i < 2 * 4; ++i) f.rgba.push_back(row);
        JPImageTransform::deinterlace(f);
        assert(f.rgba[0] == 0 && f.rgba[8] == 2 && f.rgba[16] == 1 && f.rgba[24] == 3);
        JPFrame g;
        g.width = 4;
        g.height = 4;
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 4; ++x)
                for (int k = 0; k < 4; ++k) g.rgba.push_back(uint8_t(y * 4 + x));
        JPImageTransform::crop(g, 2, 0);   // 2 across about the middle, all of it down
        assert(g.width == 2 && g.height == 4 && g.rgba.size() == 2 * 4 * 4 && g.rgba[0] == 1 && g.rgba[4] == 2);
        JPImageTransform::crop(g, 0, 9);   // nothing to cut
        assert(g.width == 2 && g.height == 4);
    }
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

    // A saved picture reads back exactly as it was.
    const std::string png = (std::filesystem::temp_directory_path() / "jplacer_test_frame.png").string();
    assert(JPImageFile::writePng(png, f, error));
    JPFrame back;
    assert(JPImageFile::readPng(png, back, error));
    assert(back.width == f.width && back.height == f.height && back.rgba == f.rgba);
    std::filesystem::remove(png);
    JPFrame empty;
    assert(!JPImageFile::writePng(png, empty, error) && !error.empty());

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
    // A camera that hangs after a few pictures (as one wedged by noise does):
    // the feed says so, opens it again, and its pictures carry on, counted on.
    {
        JPCameraConfig cam;
        cam.name = "Wedging";
        cam.device = JJson::parse(R"({ "backend": "simulated", "width": 64, "height": 48, "fps": 30, "hangAfterFrames": 5 })");
        JPCameraFeed feed(cam);
        std::atomic<int> lost{ 0 };
        std::atomic<uint64_t> newest{ 0 };
        feed.onError.connect([&](std::string why) { if (why.find("no picture") != std::string::npos) ++lost; });
        feed.onFrame.connect([&](uint64_t seq) { newest = seq; });
        feed.start();
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(12);
        while (newest < 8 && std::chrono::steady_clock::now() < until) std::this_thread::sleep_for(std::chrono::milliseconds(20));
        assert(lost >= 1 && newest >= 8 && feed.isRunning());
        feed.stop();
        assert(!feed.isRunning());
    }
    return 0;
}
