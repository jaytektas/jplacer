// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPSimulatedSource.h"

#include <thread>

inline namespace jf {

namespace {

// The test picture's grid pitch in pixels and its two shades.
constexpr int     kGrid  = 40;
constexpr uint8_t kDark  = 40;
constexpr uint8_t kLight = 90;

} // namespace

JPSimulatedSource::JPSimulatedSource(std::string name, int width, int height, double fps)
    : m_name(std::move(name)), m_mode{ "SIM", width, height, fps } {}

bool JPSimulatedSource::open(std::string& error) {
    if (m_mode.width <= 0 || m_mode.height <= 0 || m_mode.fps <= 0) {
        error = m_name + ": a simulated camera needs a width, a height and a frame rate";
        return false;
    }
    return true;
}

std::vector<JPCaptureMode> JPSimulatedSource::modes() const { return { m_mode }; }

bool JPSimulatedSource::start(const JPCaptureMode&, std::string&) {
    m_next = std::chrono::steady_clock::now();
    return true;
}

bool JPSimulatedSource::grab(JPFrame& frame, int timeoutMs, std::string&) {
    const auto now = std::chrono::steady_clock::now();
    if (m_next > now + std::chrono::milliseconds(timeoutMs)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(timeoutMs));
        return false;
    }
    std::this_thread::sleep_until(m_next);
    m_next += std::chrono::microseconds(int64_t(1e6 / m_mode.fps));

    frame.width  = m_mode.width;
    frame.height = m_mode.height;
    frame.rgba.resize(size_t(frame.width) * size_t(frame.height) * 4);
    const int bar = int(m_sequence * 4 % uint64_t(frame.width));
    uint8_t* p = frame.rgba.data();
    for (int y = 0; y < frame.height; ++y)
        for (int x = 0; x < frame.width; ++x) {
            const bool line = x % kGrid == 0 || y % kGrid == 0;
            const uint8_t v = line ? kLight : kDark;
            *p++ = v;
            *p++ = (x >= bar && x < bar + kGrid / 4) ? kLight : v;
            *p++ = v;
            *p++ = 255;
        }
    frame.sequence = ++m_sequence;
    return true;
}

} // inline namespace jf
