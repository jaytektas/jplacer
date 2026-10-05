// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPCaptureMode.h"
#include "JPFrame.h"
#include "JPWhiteBalance.h"

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
// is opened again until it is back: lost when it reports an error, hung when
// it gives no picture for a while, or the very same picture over and over
// (a real camera's sensor noise makes no two alike).
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
    // The latest picture as the camera took it, before white balance (for
    // working a white balance out). False when there is none yet.
    bool latestUnbalanced(JPFrame& out) const;
    // The mode it is capturing in, once started.
    std::optional<JPCaptureMode> mode() const;
    // Lost (or hung) and being opened again, and why: until its first picture
    // once it is back.
    bool isLost() const { return m_lost; }
    std::string lostWhy() const;

    // OpenPnP's camera scripting events (Camera.BeforeSettle, Camera.AfterCapture, ...)
    // for this camera, run where vision takes its pictures (JPCameraLook);
    // false with why when one failed. Set once, before pictures are taken.
    std::function<bool(const std::string& event, std::string& why)> scriptEvent;

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
    JPFrame                      m_unbalanced;   // m_latest before white balance (kept while there is one)
    JPWhiteBalance               m_balance;
    std::optional<JPCaptureMode> m_mode;
    std::atomic<bool>            m_lost{ false };
    std::string                  m_lostWhy;   // guarded by m_mutex
};

} // inline namespace jf
