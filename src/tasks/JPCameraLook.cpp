// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPCameraLook.h"

#include <chrono>
#include <thread>

inline namespace jf {

bool JPCameraLook::settled(JPCameraFeed& feed, JPGrayImage& out, std::string& why, int settleMs) {
    if (!feed.isRunning()) {
        why = feed.config().name + " is not running";
        return false;
    }
    const auto now = std::chrono::steady_clock::now();
    const auto takenFrom = now + std::chrono::milliseconds(settleMs);
    const auto until = now + std::chrono::milliseconds(settleMs + kTimeoutMs);
    JPFrame frame;
    while (std::chrono::steady_clock::now() < until) {
        if (feed.latest(frame, 0) && frame.captured >= takenFrom) {
            out = JPGrayImage::fromRgba(frame.rgba.data(), frame.width, frame.height);
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    why = feed.config().name + ": no picture taken within " + std::to_string(settleMs + kTimeoutMs) + " ms";
    return false;
}

} // inline namespace jf
