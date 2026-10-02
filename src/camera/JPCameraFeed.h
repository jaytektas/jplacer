// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPCaptureMode.h"
#include "JPFrame.h"

#include "machine/JPCameraConfig.h"

#include <j/core/Signal.h>

#include <atomic>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

inline namespace jf {

// One camera, running: its capture thread grabs pictures as fast as the
// camera sends them and keeps only the LATEST (a viewer that falls behind
// skips frames rather than showing old ones). A camera that is lost or hangs
// is opened again until it is back.
//
// Signals fire on the capture thread; a widget re-posts to the main thread.
class JPCameraFeed {
public:
    explicit JPCameraFeed(JPCameraConfig config);
    ~JPCameraFeed();

    JPCameraFeed(const JPCameraFeed&)            = delete;
    JPCameraFeed& operator=(const JPCameraFeed&) = delete;

    const JPCameraConfig& config() const { return m_config; }

    // Where the camera is looking (machine X, Y), for a simulated camera that
    // draws the machine. Set before start().
    void setView(std::function<bool(double&, double&)> view) { m_view = std::move(view); }

    void start();
    void stop();
    bool isRunning() const { return m_running; }

    // The newest frame, into `out`, if it is newer than sequence `have`.
    bool latest(JPFrame& out, uint64_t have) const;
    // The mode it is capturing in, once started.
    std::optional<JPCaptureMode> mode() const;

    JSignal<uint64_t>    onFrame;     // a new frame's sequence
    JSignal<std::string> onError;     // why it was lost (it is opened again until stopped)
    JSignal<bool>        onRunning;

private:
    void run();
    // One opening of the camera, until it stops or is lost (`why`).
    void runSource(std::string& why);

    JPCameraConfig    m_config;
    std::function<bool(double&, double&)> m_view;
    std::thread       m_thread;
    std::atomic<bool> m_running{ false };
    uint64_t          m_sequence = 0;   // the capture thread's

    mutable std::mutex           m_mutex;   // guards the members below
    JPFrame                      m_latest;
    std::optional<JPCaptureMode> m_mode;
};

} // inline namespace jf
