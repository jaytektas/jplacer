// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPCameraLook.h"

#include "common/JPlacerLog.h"

#include <j/core/Log.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>

inline namespace jf {

bool JPCameraLook::taken(JPCameraFeed& feed, JPGrayImage& out, std::string& why, int afterMs) {
    if (!feed.isRunning()) {
        why = feed.config().name + " is not running";
        return false;
    }
    const auto now = std::chrono::steady_clock::now();
    const auto takenFrom = now + std::chrono::milliseconds(afterMs);
    auto until = now + std::chrono::milliseconds(afterMs + kTimeoutMs);
    // A camera lost (unplugged, hung) is waited for while it is opened again,
    // up to kLostWaitMs: plugged back in, the task carries on where it was.
    const auto longest = now + std::chrono::milliseconds(afterMs + kLostWaitMs);
    bool said = false;
    JPFrame frame;
    while (std::chrono::steady_clock::now() < until) {
        if (feed.latest(frame, 0) && frame.captured >= takenFrom) {
            if (said) JLOGC(JPlacerLog::kCamera, JLogLevel::Info) << feed.config().name << " is back";
            out = JPGrayImage::fromRgba(frame.rgba.data(), frame.width, frame.height);
            return true;
        }
        if (feed.isLost()) {
            if (!said) {
                JLOGC(JPlacerLog::kCamera, JLogLevel::Warn) << feed.config().name << " lost (" << feed.lostWhy()
                                                            << "): waiting for it to come back";
                said = true;
            }
            until = std::min(longest, std::chrono::steady_clock::now() + std::chrono::milliseconds(kTimeoutMs));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (feed.isLost())
        why = feed.config().name + " was lost (" + feed.lostWhy() + ") and not back within "
            + std::to_string(kLostWaitMs / 1000) + " s: plug it in again, then try again";
    else
        why = feed.config().name + ": no picture taken within " + std::to_string(afterMs + kTimeoutMs) + " ms";
    return false;
}

double JPCameraLook::difference(const JPGrayImage& a, const JPGrayImage& b, const std::string& method, double maskCircle) {
    if (a.width != b.width || a.height != b.height || a.pixels.empty()) return 100;
    const double r = maskCircle > 0 ? maskCircle * std::min(a.width, a.height) * 0.5 : 0;
    const double cx = (a.width - 1) * 0.5, cy = (a.height - 1) * 0.5;
    double sum = 0, sumSq = 0, most = 0;
    size_t n = 0;
    for (int y = 0; y < a.height; ++y)
        for (int x = 0; x < a.width; ++x) {
            if (r > 0 && (x - cx) * (x - cx) + (y - cy) * (y - cy) > r * r) continue;
            const size_t i = size_t(y) * size_t(a.width) + size_t(x);
            const double d = std::abs(double(a.pixels[i]) - double(b.pixels[i]));
            sum += d;
            sumSq += d * d;
            most = std::max(most, d);
            ++n;
        }
    if (n == 0) return 0;
    // OpenPnP's norms, each over its full scale.
    double v = 0;
    if (method == "Maximum")     v = most / 255.0;
    else if (method == "Mean")   v = sum / (255.0 * double(n));
    else if (method == "Square") v = sumSq / (255.0 * 255.0 * double(n));
    else                         v = std::sqrt(sumSq) / (255.0 * std::sqrt(double(n)));   // Euclidean
    return v * 100;
}

bool JPCameraLook::settled(JPCameraFeed& feed, JPGrayImage& out, std::string& why, JPSettleTrace* trace) {
    const JPCameraConfig::Settle& st = feed.config().settle;
    const bool fixed = st.method == "FixedTime" || st.method.empty();
    if (fixed && !trace) return taken(feed, out, why, st.timeMs);
    // Each picture taken since the call against the one before, until still
    // (or, timed, until the time is up).
    const auto start = std::chrono::steady_clock::now();
    const std::string method = fixed ? std::string("Euclidean") : st.method;
    if (trace) {
        *trace = JPSettleTrace();
        trace->method = fixed ? std::string("FixedTime") : st.method;
        trace->threshold = fixed ? 0 : st.threshold;
    }
    JPGrayImage last;
    if (!taken(feed, last, why, 0)) return false;
    const auto until = start + std::chrono::milliseconds(fixed ? st.timeMs : st.timeoutMs);
    int still = 0;
    JPFrame frame;
    uint64_t have = 0;
    while (true) {
        if (!feed.latest(frame, have)) {
            if (std::chrono::steady_clock::now() > until) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }
        have = frame.sequence;
        JPGrayImage next = JPGrayImage::fromRgba(frame.rgba.data(), frame.width, frame.height);
        const double d = difference(last, next, method, st.maskCircle);
        last = std::move(next);
        const double ms = std::chrono::duration<double, std::milli>(frame.captured - start).count();
        if (trace) trace->points.push_back({ ms, d });
        if (fixed) {
            if (frame.captured >= until) {
                if (trace) trace->settledMs = ms;
                out = std::move(last);
                return true;
            }
            continue;
        }
        still = d <= st.threshold ? still + 1 : 0;
        if (still > st.debounce) {
            if (trace) trace->settledMs = ms;
            out = std::move(last);
            return true;
        }
        if (std::chrono::steady_clock::now() > until) break;
    }
    if (fixed) {
        if (trace) trace->settledMs = st.timeMs;
    } else {
        JLOGC(JPlacerLog::kCamera, JLogLevel::Warn) << feed.config().name << ": not settled within " << st.timeoutMs
                                                    << " ms; the last picture is used";
    }
    out = std::move(last);
    return true;
}

bool JPCameraLook::calibration(JPCell& cell, JPCameraFeed& feed, JPCameraCalibration& out, std::string& why) {
    JPGrayImage picture;
    if (!taken(feed, picture, why, 0)) return false;
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
