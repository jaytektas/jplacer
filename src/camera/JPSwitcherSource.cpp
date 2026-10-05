// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPSwitcherSource.h"

#include "JPCameraFeed.h"

#include <chrono>
#include <map>
#include <mutex>
#include <thread>

inline namespace jf {

namespace {

// The multiplexers' state: by switcher number, the camera switched in ("" while switching or not known).
std::timed_mutex                 s_switching;
std::map<int, std::string>       s_switched;

} // namespace

JPSwitcherSource::JPSwitcherSource(std::string cameraName, Settings settings, Links links, std::function<bool()> takeClaim)
    : m_name(std::move(cameraName)), m_settings(std::move(settings)), m_links(std::move(links)), m_takeClaim(std::move(takeClaim)) {}

bool JPSwitcherSource::open(std::string& error) {
    if (!m_links.camera || !m_links.actuate) {
        error = m_name + ": a switcher camera needs the machine";
        return false;
    }
    m_device = m_links.camera(m_settings.cameraId);
    if (!m_device) {
        error = m_name + ": no camera " + m_settings.cameraId + " to switch";
        return false;
    }
    if (m_settings.actuatorId.empty()) {
        error = m_name + ": no actuator to switch the camera with";
        return false;
    }
    return true;
}

std::vector<JPCaptureMode> JPSwitcherSource::modes() const {
    // The device camera's, once it runs; a stand-in until then (its pictures are taken as they come).
    if (m_device)
        if (const auto mode = m_device->mode()) return { *mode };
    JPCaptureMode m;
    m.format = "switched";
    m.width = 640;
    m.height = 480;
    return { m };
}

bool JPSwitcherSource::grab(JPFrame& frame, int timeoutMs, std::string& error) {
    const bool wanted = m_takeClaim && m_takeClaim();
    {
        std::unique_lock lk(s_switching, std::defer_lock);
        if (!lk.try_lock_for(std::chrono::milliseconds(m_settings.delayMs * 4))) return false;
        std::string& switched = s_switched[m_settings.switcher];
        if (switched != m_name) {
            if (!wanted && !switched.empty()) {
                // Another camera is switched in: idle until this one is wanted.
                m_idle = true;
                lk.unlock();
                std::this_thread::sleep_for(std::chrono::milliseconds(timeoutMs));
                return false;
            }
            switched.clear();   // switching may fail: not known meanwhile
            std::string why;
            if (!m_links.actuate(m_settings.actuatorId, m_settings.actuatorValue, why)) {
                error = m_name + ": its switcher actuator failed: " + why;
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(m_settings.delayMs));
            switched = m_name;
            m_have = 0;
            if (m_device) {
                JPFrame latest;
                if (m_device->latest(latest, 0)) m_have = latest.sequence;   // only pictures after the switch
            }
        }
    }
    m_idle = false;
    // The device camera's next picture, as taken.
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < until) {
        JPFrame f;
        if (m_device->latest(f, m_have)) {
            m_have = f.sequence;
            JPFrame raw;
            frame = m_device->latestUnbalanced(raw) ? raw : f;
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}

std::string JPSwitcherSource::describe() const {
    return m_name + " (switched in on " + m_settings.cameraId + ", switcher " + std::to_string(m_settings.switcher) + ")";
}

} // inline namespace jf
