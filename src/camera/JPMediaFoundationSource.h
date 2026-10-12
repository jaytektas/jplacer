// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPCaptureSource.h"

#include <j/config/Json.h>

#include <memory>
#include <string>
#include <vector>

inline namespace jf {

// A capture device on Windows (Media Foundation), as JPV4L2Source is on Linux: found by the name the device
// gives itself ("HD USB Camera"; the second of two alike "HD USB Camera #2"), its formats as it offers them
// (MJPG, YUYV, NV12…, each size and rate), its pictures decoded and turned to RGBA by Media Foundation, and its own
// settings by jplacer's names (exposure, white-balance, focus, gain, brightness, contrast, saturation, hue,
// gamma, sharpness, backlight-compensation, zoom: Windows' camera controls). Exposure is given in the units
// Video4Linux gives it, 100 µs (Windows keeps it as a power of two of seconds): the same settings mean the same
// on either system, and what scales it (Auto-Tune, Expose each picture) can. A setting the device does not have
// is noted, not a failure.
class JPMediaFoundationSource : public JPCaptureSource {
public:
    // `controls`: the device's own settings, set each time it starts (as JPV4L2Source's).
    JPMediaFoundationSource(std::string name, JJson controls);
    ~JPMediaFoundationSource() override;

    bool open(std::string& error) override;
    void close() override;
    std::vector<JPCaptureMode> modes() const override;
    bool start(const JPCaptureMode& mode, std::string& error) override;
    bool grab(JPFrame& frame, int timeoutMs, std::string& error) override;
    std::string describe() const override;
    JJson controls() const override;
    void reapplyControls() override { applyControls(); }
    void setControls(const JJson& controls) override {
        m_controls = controls;
        applyControls();
    }

    // Every capture device present, by the name it gives itself (two alike: the second "Name #2").
    static std::vector<std::string> deviceNames();

private:
    struct Impl;   // Media Foundation's objects (Windows' types kept out of this header)
    void applyControls();

    std::string           m_name;
    JJson                 m_controls;
    std::unique_ptr<Impl> m_impl;
};

} // inline namespace jf
