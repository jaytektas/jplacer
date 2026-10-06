// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPCaptureSource.h"

#include <j/config/Json.h>

#include <cstddef>
#include <string>
#include <vector>

inline namespace jf {

// A Video4Linux2 camera (Linux), found by the NAME the device gives itself
// ("top: top"), not by /dev/videoN or USB port, both of which change with
// the order devices start and the socket they are plugged into. Captures
// through memory-mapped buffers; MJPG and YUYV are turned into RGBA.
class JPV4L2Source : public JPCaptureSource {
public:
    // `controls`: the camera's own settings, set each time it starts (a
    // camera forgets them when it drops off its bus):
    //   { "exposure": { "auto": false, "value": 1432 }, "white-balance": { ... }, ... }
    // by jplacer's names: exposure, white-balance, focus, gain, brightness,
    // contrast, saturation, hue, gamma, sharpness, backlight-compensation,
    // power-line-frequency. A setting the camera does not have is noted, not
    // a failure.
    JPV4L2Source(std::string name, JJson controls);
    ~JPV4L2Source() override;

    bool open(std::string& error) override;
    void close() override;
    std::vector<JPCaptureMode> modes() const override;
    bool start(const JPCaptureMode& mode, std::string& error) override;
    bool grab(JPFrame& frame, int timeoutMs, std::string& error) override;
    std::string describe() const override;
    JJson controls() const override;
    void reapplyControls() override { applyControls(); }

    // Every capture device present, by the name it gives itself.
    static std::vector<std::string> deviceNames();

private:
    struct Buffer {
        void*  data = nullptr;
        size_t size = 0;
    };

    // The /dev/videoN whose device calls itself `name` and captures pictures.
    static std::string findDevice(const std::string& name);
    void unmap();
    void applyControls();

    std::string         m_name;
    std::string         m_path;
    int                 m_fd = -1;
    std::vector<Buffer> m_buffers;
    uint32_t            m_fourcc = 0;
    int                 m_width = 0, m_height = 0;
    uint64_t            m_sequence = 0;
    bool                m_streaming = false;
    JJson               m_controls;
};

} // inline namespace jf
