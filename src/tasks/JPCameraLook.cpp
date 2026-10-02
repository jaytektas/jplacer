// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPCameraLook.h"

#include <chrono>
#include <thread>

inline namespace jf {

bool JPCameraLook::settled(JPCameraFeed& feed, JPGrayImage& out, std::string& why, int settleFrames) {
    if (!feed.isRunning()) {
        why = feed.config().name + " is not running";
        return false;
    }
    JPFrame frame;
    uint64_t first = 0;
    feed.latest(frame, 0);
    first = frame.sequence;
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(kTimeoutMs);
    while (std::chrono::steady_clock::now() < until) {
        if (feed.latest(frame, 0) && frame.sequence >= first + uint64_t(settleFrames)) {
            out = JPGrayImage::fromRgba(frame.rgba.data(), frame.width, frame.height);
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    why = feed.config().name + ": no new picture within " + std::to_string(kTimeoutMs) + " ms";
    return false;
}

} // inline namespace jf
