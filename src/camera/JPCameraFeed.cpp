// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPCameraFeed.h"

#include "JPCaptureFactory.h"

#include "common/JPlacerLog.h"

#include <j/core/Log.h>

#include <chrono>
#include <thread>

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
// A camera that sends nothing this long has hung; one that is lost is
// looked for again this often.
constexpr int kStalledMs   = 3000;
constexpr int kReconnectMs = 2000;

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
    // A camera can drop off its bus (a stepper's noise on a USB cable) or
    // hang with no error: either way it is closed and opened again, by its
    // name, until it is back or the feed is stopped.
    std::string lastWhy;
    while (m_running) {
        std::string why;
        runSource(why);
        if (!m_running) break;
        if (why != lastWhy) {
            JLOGC(JPlacerLog::kCamera, JLogLevel::Warn) << m_config.name << ": " << why << "; trying again";
            lastWhy = why;
        }
        onError.emit(why);
        for (int waited = 0; m_running && waited < kReconnectMs; waited += kGrabSliceMs)
            std::this_thread::sleep_for(std::chrono::milliseconds(kGrabSliceMs));
    }
    onRunning.emit(false);
}

void JPCameraFeed::runSource(std::string& why) {
    auto source = JPCaptureFactory::create(m_config.name, m_config.device, why, m_view);
    if (!source || !source->open(why)) return;
    const auto mode = JPCaptureFactory::choose(source->modes(), m_config.device);
    if (!mode) {
        why = source->describe() + " offers no picture format jplacer can read";
        return;
    }
    if (!source->start(*mode, why)) return;
    {
        std::lock_guard lk(m_mutex);
        m_mode = *mode;
    }
    JLOGC(JPlacerLog::kCamera, JLogLevel::Info) << m_config.name << ": " << mode->describe();
    onRunning.emit(true);

    JPFrame frame;
    auto lastFrame = std::chrono::steady_clock::now();
    while (m_running) {
        std::string error;
        if (!source->grab(frame, kGrabSliceMs, error)) {
            if (!error.empty()) {
                why = error;
                break;
            }
            if (std::chrono::steady_clock::now() - lastFrame > std::chrono::milliseconds(kStalledMs)) {
                why = source->describe() + ": no picture for " + std::to_string(kStalledMs / 1000)
                    + " s (the camera may have hung)";
                break;
            }
            continue;
        }
        lastFrame = std::chrono::steady_clock::now();
        frame.sequence = ++m_sequence;   // the feed's own count, unbroken when the camera is opened again
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
}

} // inline namespace jf
