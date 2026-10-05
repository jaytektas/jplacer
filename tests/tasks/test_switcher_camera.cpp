// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// OpenPnP's SwitcherCamera: two cameras on one capture device, through one
// multiplexer. A picture asked of either switches the actuator to its value
// first (and waits the actuator delay); the one not switched in waits idle,
// not lost; its pictures are the device camera's.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "camera/JPCameraFeed.h"
#include "tasks/JPCameraLook.h"

#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

using namespace jf;

namespace {

JPCameraConfig device() {
    JPCameraConfig c;
    c.id = "DEV";
    c.name = "Capture device";
    c.device = JJson::object();
    c.device["backend"] = "simulated";
    c.device["width"] = 64;
    c.device["height"] = 48;
    c.device["fps"] = 30;
    return c;
}

JPCameraConfig switched(const char* id, double value) {
    JPCameraConfig c;
    c.id = id;
    c.name = id;
    c.device = JJson::object();
    c.device["backend"] = "switcher";
    c.device["camera"] = "DEV";
    c.device["switcher"] = 0;
    c.device["actuator"] = "MUX";
    c.device["actuatorValue"] = value;
    c.device["actuatorDelayMs"] = 50;
    return c;
}

} // namespace

int main() {
    JPCameraFeed dev(device());
    dev.start();

    std::mutex lock;
    std::vector<double> actuated;
    JPSwitcherSource::Links links {
        [&dev](const std::string& id) { return id == "DEV" ? &dev : nullptr; },
        [&](const std::string& actuatorId, double value, std::string&) {
            assert(actuatorId == "MUX");
            std::lock_guard g(lock);
            actuated.push_back(value);
            return true;
        } };
    JPCameraFeed a(switched("A", 1)), b(switched("B", 2));
    a.setSwitching(links);
    b.setSwitching(links);
    a.start();
    b.start();
    // Each switches itself in once, as it starts (it is wanted from its start).
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    JPFrame frame;
    while (!(a.latest(frame, 0) && b.latest(frame, 0)) && std::chrono::steady_clock::now() < until)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    assert(a.latest(frame, 0) && b.latest(frame, 0));
    auto last = [&] { std::lock_guard g(lock); return actuated.empty() ? -1.0 : actuated.back(); };

    for (int round = 0; round < 3; ++round) {
        JPGrayImage img;
        std::string why;
        assert(JPCameraLook::taken(a, img, why, 0));
        assert(img.width == 64 && img.height == 48);
        assert(last() == 1);
        assert(JPCameraLook::taken(b, img, why, 0));
        assert(img.width == 64 && last() == 2);
    }
    // B switched in: A waits, idle, not lost, and does not switch it back.
    const size_t switches = [&] { std::lock_guard g(lock); return actuated.size(); }();
    std::this_thread::sleep_for(std::chrono::milliseconds(800));
    assert(!a.isLost() && !b.isLost());
    assert(last() == 2);
    {
        std::lock_guard g(lock);
        assert(actuated.size() == switches);
    }
    a.stop();
    b.stop();
    dev.stop();
    return 0;
}
