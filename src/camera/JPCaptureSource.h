// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPCaptureMode.h"
#include "JPFrame.h"

#include <string>
#include <vector>

inline namespace jf {

// Where a camera's pictures come from: a capture device (JPV4L2Source) or a
// simulation (JPSimulatedSource). Used from one camera's capture thread.
class JPCaptureSource {
public:
    virtual ~JPCaptureSource() = default;

    // Find and open the device. False with `error` in plain words.
    virtual bool open(std::string& error) = 0;
    virtual void close() = 0;

    // The modes the device offers, and starting it in one.
    virtual std::vector<JPCaptureMode> modes() const = 0;
    virtual bool start(const JPCaptureMode& mode, std::string& error) = 0;

    // The next picture, into `frame` (RGBA). False on timeout or failure;
    // `error` empty for a plain timeout.
    virtual bool grab(JPFrame& frame, int timeoutMs, std::string& error) = 0;

    // Waiting for its turn (a switcher camera not switched in): no picture,
    // and not a camera that has hung.
    virtual bool idle() const { return false; }

    // For logs and the UI: "top: top (/dev/video0)".
    virtual std::string describe() const = 0;
};

} // inline namespace jf
