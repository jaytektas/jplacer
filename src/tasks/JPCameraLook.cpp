// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPCameraLook.h"

#include "common/JPlacerLog.h"

#include <j/core/Log.h>

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

bool JPCameraLook::calibration(JPCell& cell, JPCameraFeed& feed, JPCameraCalibration& out, std::string& why) {
    JPGrayImage picture;
    if (!settled(feed, picture, why, 0)) return false;
    out = cell.cameraCalibration(feed.config().id, picture.width, picture.height);
    if (out.valid) return true;
    why = feed.config().name + " is not calibrated for its " + std::to_string(picture.width) + "\xC3\x97"
        + std::to_string(picture.height) + " pictures: calibrate it";
    return false;
}

bool JPCameraLook::lightOnly(JPCell& cell, JPCameraFeed& feed, JPGrayImage& out, std::string& why) {
    const std::string light = feed.config().lightActuator();
    if (light.empty()) {
        why = feed.config().name + " has no light to switch";
        return false;
    }
    JPGrayImage unlit, lit;
    const bool ok = cell.switchActuatorAndWait(light, false, why) && settled(feed, unlit, why)
                 && cell.switchActuatorAndWait(light, true, why) && settled(feed, lit, why);
    if (!ok) {
        std::string ignored;
        cell.switchActuatorAndWait(light, true, ignored);   // as it was
        return false;
    }
    out = JPGrayImage::difference(lit, unlit);
    return true;
}

JPRoundMark JPCameraLook::findTryingHarder(JPCell& cell, JPCameraFeed& feed, const JPGrayImage& picture,
                                           const JPRoundMarkFinder::Request& request) {
    const JPRoundMark plain = JPRoundMarkFinder::find(picture, request);
    if (plain.found || feed.config().lightActuator().empty()) return plain;
    JPGrayImage lit;
    std::string why;
    if (!lightOnly(cell, feed, lit, why)) {
        JLOGC(JPlacerLog::kCamera, JLogLevel::Warn) << feed.config().name << ": no picture without the room's light: " << why;
        return plain;
    }
    JPRoundMark m = JPRoundMarkFinder::find(lit, request);
    JLOGC(JPlacerLog::kCamera, JLogLevel::Info) << feed.config().name << ": not found in the picture (" << plain.why
        << "); with the room's light taken out: " << (m.found ? "found" : m.why);
    if (!m.found) m.why = plain.why + "; with the room's light taken out, " + m.why;
    return m;
}

} // inline namespace jf
