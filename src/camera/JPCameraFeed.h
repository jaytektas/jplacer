// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPCaptureMode.h"
#include "JPFrame.h"

#include "machine/JPCameraConfig.h"

#include <j/core/Signal.h>

#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

inline namespace jf {

// One camera, running: its capture thread grabs pictures as fast as the
// camera sends them and keeps only the LATEST (a viewer that falls behind
// skips frames rather than showing old ones).
//
// Signals fire on the capture thread; a widget re-posts to the main thread.
class JPCameraFeed {
public:
    explicit JPCameraFeed(JPCameraConfig config);
    ~JPCameraFeed();

    JPCameraFeed(const JPCameraFeed&)            = delete;
    JPCameraFeed& operator=(const JPCameraFeed&) = delete;

    const JPCameraConfig& config() const { return m_config; }

    void start();
    void stop();
    bool isRunning() const { return m_running; }

    // The newest frame, into `out`, if it is newer than sequence `have`.
    bool latest(JPFrame& out, uint64_t have) const;
    // The mode it is capturing in, once started.
    std::optional<JPCaptureMode> mode() const;

    JSignal<uint64_t>    onFrame;     // a new frame's sequence
    JSignal<std::string> onError;     // why it stopped
    JSignal<bool>        onRunning;

private:
    void run();

    JPCameraConfig    m_config;
    std::thread       m_thread;
    std::atomic<bool> m_running{ false };

    mutable std::mutex           m_mutex;   // guards the members below
    JPFrame                      m_latest;
    std::optional<JPCaptureMode> m_mode;
};

} // inline namespace jf
