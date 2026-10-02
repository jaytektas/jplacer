// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPCameraFeed.h"

#include "JPCaptureFactory.h"

#include "common/JPlacerLog.h"

#include <j/core/Log.h>

inline namespace jf {

namespace {

// A frame's average brightness, from every 97th pixel (enough to tell a dark
// scene from a broken decode; cheap enough to log every frame when traced).
int meanBrightness(const JPFrame& f) {
    constexpr size_t kStride = 97 * 4;
    uint64_t sum = 0, n = 0;
    for (size_t i = 0; i + 2 < f.rgba.size(); i += kStride, ++n) sum += (f.rgba[i] + f.rgba[i + 1] + f.rgba[i + 2]) / 3;
    return n ? int(sum / n) : 0;
}

// How long one wait for a frame lasts before the thread checks whether it
// should stop: the time a stop can take, not a frame timeout.
constexpr int kGrabSliceMs = 100;

} // namespace

JPCameraFeed::JPCameraFeed(JPCameraConfig config) : m_config(std::move(config)) {}

JPCameraFeed::~JPCameraFeed() { stop(); }

void JPCameraFeed::start() {
    if (m_running.exchange(true)) return;
    m_thread = std::thread(&JPCameraFeed::run, this);
}

void JPCameraFeed::stop() {
    m_running = false;
    if (m_thread.joinable()) m_thread.join();
}

bool JPCameraFeed::latest(JPFrame& out, uint64_t have) const {
    std::lock_guard lk(m_mutex);
    if (m_latest.sequence == 0 || m_latest.sequence == have) return false;
    out = m_latest;
    return true;
}

std::optional<JPCaptureMode> JPCameraFeed::mode() const {
    std::lock_guard lk(m_mutex);
    return m_mode;
}

void JPCameraFeed::run() {
    auto fail = [this](const std::string& why) {
        JLOGC(JPlacerLog::kCamera, JLogLevel::Error) << m_config.name << ": " << why;
        m_running = false;
        onError.emit(why);
        onRunning.emit(false);
    };
    std::string error;
    auto source = JPCaptureFactory::create(m_config.name, m_config.device, error);
    if (!source) return fail(error);
    if (!source->open(error)) return fail(error);
    const auto mode = JPCaptureFactory::choose(source->modes(), m_config.device);
    if (!mode) return fail(source->describe() + " offers no picture format jplacer can read");
    if (!source->start(*mode, error)) return fail(error);
    {
        std::lock_guard lk(m_mutex);
        m_mode = *mode;
    }
    JLOGC(JPlacerLog::kCamera, JLogLevel::Info) << m_config.name << ": " << mode->describe();
    onRunning.emit(true);

    JPFrame frame;
    while (m_running) {
        if (!source->grab(frame, kGrabSliceMs, error)) {
            if (!error.empty()) return fail(error);
            continue;
        }
        JLOGC(JPlacerLog::kFrames, JLogLevel::Trace) << m_config.name << " frame " << frame.sequence << " "
                                                     << frame.width << "x" << frame.height << ", brightness "
                                                     << meanBrightness(frame) << "/255";
        {
            std::lock_guard lk(m_mutex);
            std::swap(m_latest, frame);
        }
        onFrame.emit(m_latest.sequence);
    }
    source->close();
    onRunning.emit(false);
}

} // inline namespace jf
