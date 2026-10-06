// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPNeoden4Source.h"

#include "JPNeoden4CameraLibrary.h"

#include "common/JPlacerLog.h"

#include <j/core/Log.h>

#include <chrono>

inline namespace jf {

JPNeoden4Source::JPNeoden4Source(std::string cameraName, Settings settings)
    : m_name(std::move(cameraName)), m_settings(settings) {}

bool JPNeoden4Source::open(std::string& error) {
    JPNeoden4CameraLibrary* lib = JPNeoden4CameraLibrary::instance(error);
    // Its name is put before the error by whoever opens it.
    if (!lib) return false;
    if (m_settings.width <= 0 || m_settings.height <= 0) {
        error = "its picture has no size";
        return false;
    }
    const Settings& s = m_settings;
    lib->window(s.camera, s.shiftX, s.shiftY, s.width, s.height);
    // OpenPnP's open: the camera reset (with its exposure and gain, a switcher camera's own).
    if (s.exposure && s.gain) lib->exposureAndGain(s.camera, *s.exposure, *s.gain);
    lib->reset(s.camera);
    m_open = true;
    return true;
}

std::vector<JPCaptureMode> JPNeoden4Source::modes() const {
    JPCaptureMode m;
    m.format = "GREY";
    m.width = m_settings.width;
    m.height = m_settings.height;
    return { m };
}

bool JPNeoden4Source::grab(JPFrame& frame, int, std::string& error) {
    // Its own Timeout, as OpenPnP waits.
    std::string why;
    JPNeoden4CameraLibrary* lib = m_open ? JPNeoden4CameraLibrary::instance(why) : nullptr;
    if (!lib) {
        error = "not open";
        return false;
    }
    const Settings& s = m_settings;
    // A switcher camera: the device switched to it, its exposure and gain.
    if (s.exposure && s.gain) lib->exposureAndGain(s.camera, *s.exposure, *s.gain);
    if (!lib->read(s.camera, s.width, s.height, s.timeoutMs, m_grey, why)) {
        JLOGC(JPlacerLog::kCamera, JLogLevel::Warn) << m_name << ": " << why;
        error = why;
        return false;
    }
    frame.width = s.width;
    frame.height = s.height;
    frame.rgba.resize(m_grey.size() * 4);
    for (size_t i = 0; i < m_grey.size(); ++i) {
        uint8_t* p = &frame.rgba[i * 4];
        p[0] = p[1] = p[2] = m_grey[i];
        p[3] = 255;
    }
    frame.captured = std::chrono::steady_clock::now();
    return true;
}

std::string JPNeoden4Source::describe() const {
    return m_name + " (NeoDen camera " + std::to_string(m_settings.camera) + ")";
}

} // inline namespace jf
