// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPCameraLook.h"

#include "JPSettleCompare.h"

#include "common/JPlacerLog.h"

#include <j/core/Log.h>

#include <opencv2/core.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <future>
#include <optional>
#include <thread>

inline namespace jf {

bool JPCameraLook::taken(JPCameraFeed& feed, JPGrayImage& out, std::string& why, int afterMs) {
    JPFrame frame;
    if (!takenFrame(feed, frame, why, afterMs)) return false;
    out = JPGrayImage::fromRgba(frame.rgba.data(), frame.width, frame.height);
    return true;
}

bool JPCameraLook::takenFrame(JPCameraFeed& feed, JPFrame& frame, std::string& why, int afterMs) {
    // Brought on screen for this, it starts a moment later: waited for.
    for (const auto start = std::chrono::steady_clock::now(); !feed.isRunning();) {
        if (std::chrono::steady_clock::now() - start > std::chrono::milliseconds(kTimeoutMs)) {
            why = feed.config().name + " is not running";
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(kStartPollMs));
    }
    feed.claim();   // a switcher camera switched in for this
    const auto now = std::chrono::steady_clock::now();
    const auto takenFrom = now + std::chrono::milliseconds(afterMs);
    auto until = now + std::chrono::milliseconds(afterMs + kTimeoutMs);
    // A camera lost (unplugged, hung) is waited for while it is opened again,
    // as long as the camera says (JPCameraConfig::Lost), from when it was
    // seen lost: plugged back in, the task carries on where it was.
    const auto wait = std::chrono::seconds(std::max(0, feed.config().lost.waitS));
    std::optional<std::chrono::steady_clock::time_point> lostAt;
    for (auto at = now; at < until; at = std::chrono::steady_clock::now()) {
        if (feed.latest(frame, 0) && frame.captured >= takenFrom) {
            if (lostAt) JLOGC(JPlacerLog::kCamera, JLogLevel::Info) << feed.config().name << " is back";
            return true;
        }
        if (feed.isLost()) {
            if (!lostAt) {
                lostAt = at;
                if (wait.count() > 0)
                    JLOGC(JPlacerLog::kCamera, JLogLevel::Warn) << feed.config().name << " lost (" << feed.lostWhy()
                                                                << "): waiting for it to come back";
            }
            if (at >= *lostAt + wait) break;
            until = std::max(until, at + std::chrono::milliseconds(kTimeoutMs));   // time for a picture once back
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (feed.isLost())
        why = feed.config().name + " was lost (" + feed.lostWhy() + ")"
            + (wait.count() > 0 ? " and not back within " + std::to_string(wait.count()) + " s" : std::string())
            + ": plug it in again, then try again";
    else
        why = feed.config().name + ": no picture taken within " + std::to_string(afterMs + kTimeoutMs) + " ms";
    return false;
}

bool JPCameraLook::settled(JPCameraFeed& feed, JPGrayImage& out, std::string& why, JPSettleTrace* trace) {
    // OpenPnP's settleAndCapture, with its scripting events: settled, then the picture taken.
    auto event = [&feed, &why](const char* name) { return !feed.scriptEvent || feed.scriptEvent(name, why); };
    feed.claim();   // a switcher camera switched in for this
    return event("Camera.BeforeSettle") && settledNow(feed, out, why, trace) && exposedNow(feed, out, why)
        && event("Camera.AfterSettle")
        && event("Camera.BeforeCapture") && event("Camera.AfterCapture");
}

bool JPCameraLook::exposedNow(JPCameraFeed& feed, JPGrayImage& out, std::string& why) {
    if (!feed.config().exposeEachPicture) return true;
    // Told on the capture thread; shared, so a late answer has somewhere to go.
    auto told = std::make_shared<std::promise<JPOneShotExposure::Result>>();
    std::future<JPOneShotExposure::Result> exposed = told->get_future();
    feed.expose(feed.config().exposeBrightness, [told](const JPOneShotExposure::Result& r) { told->set_value(r); });
    if (exposed.wait_for(std::chrono::milliseconds(kExposeMs)) != std::future_status::ready) {
        why = feed.config().name + " was not exposed within " + std::to_string(kExposeMs) + " ms";
        return false;
    }
    // Not reached (too dark even at its longest, say): the picture as near as it got, for vision to judge.
    if (const JPOneShotExposure::Result r = exposed.get(); !r.ok && r.pictures == 0) {
        why = feed.config().name + " could not be exposed: " + r.why;
        return false;
    }
    return taken(feed, out, why, 0);
}

bool JPCameraLook::settledNow(JPCameraFeed& feed, JPGrayImage& out, std::string& why, JPSettleTrace* trace) {
    const JPCameraConfig::Settle& st = feed.config().settle;
    const bool fixed = st.method == "FixedTime" || st.method.empty();
    // OpenPnP's Diagnostics: every settle traced, its pictures kept, handed to the feed's owner.
    JPSettleTrace kept;
    if (!trace && st.diagnostics && !fixed) trace = &kept;
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
    auto pictures = std::make_shared<std::vector<JPSettleTrace::Picture>>();
    JPSettleCompare compare(st, method);
    auto record = [&](const JPFrame& frame, const cv::Mat& prepared) {
        if (!trace || !st.diagnostics) return;
        const double ms = std::chrono::duration<double, std::milli>(frame.captured - start).count();
        trace->captures.push_back({ ms, 0.0 });
        trace->captures.push_back({ ms, 1.0 });
        trace->captures.push_back({ ms, 0.0 });
        cv::Mat bytes;
        prepared.convertTo(bytes, CV_8U);
        JPSettleTrace::Picture p { ms, bytes.cols, bytes.rows, bytes.channels(), {} };
        p.pixels.assign(bytes.data, bytes.data + bytes.total() * bytes.elemSize());
        pictures->push_back(std::move(p));
    };
    JPFrame frame;
    if (!takenFrame(feed, frame, why, 0)) return false;
    cv::Mat last = compare.prepare(frame);
    record(frame, last);
    const auto until = start + std::chrono::milliseconds(fixed ? st.timeMs : st.timeoutMs);
    int still = 0;
    bool done = false;
    uint64_t have = frame.sequence;
    while (!done) {
        if (!feed.latest(frame, have)) {
            if (std::chrono::steady_clock::now() > until) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }
        have = frame.sequence;
        cv::Mat next = compare.prepare(frame);
        record(frame, next);
        const double d = compare.difference(last, next);
        last = next;
        const double ms = std::chrono::duration<double, std::milli>(frame.captured - start).count();
        if (trace) trace->points.push_back({ ms, d });
        if (fixed) {
            if (frame.captured >= until) {
                if (trace) trace->settledMs = ms;
                done = true;
            }
            continue;
        }
        // As OpenPnP's: over the threshold starts the count again; a picture
        // the same as the one before (no noise at all: a picture repeated) does not count.
        if (d > st.threshold) still = 0;
        else if (d > 0) ++still;
        if (still > st.debounce) {
            if (trace) trace->settledMs = ms;
            done = true;
        } else if (std::chrono::steady_clock::now() > until) {
            break;
        }
    }
    if (!done) {
        if (fixed) {
            if (trace) trace->settledMs = st.timeMs;
        } else {
            JLOGC(JPlacerLog::kCamera, JLogLevel::Warn) << feed.config().name << ": not settled within " << st.timeoutMs
                                                        << " ms; the last picture is used";
        }
    }
    if (trace && st.diagnostics) trace->pictures = pictures;
    if (trace == &kept && feed.onSettleTrace) feed.onSettleTrace(kept);
    out = JPGrayImage::fromRgba(frame.rgba.data(), frame.width, frame.height);
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
