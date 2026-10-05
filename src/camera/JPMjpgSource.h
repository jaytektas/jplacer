// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPCaptureSource.h"

#include <string>
#include <vector>

inline namespace jf {

// OpenPnP's MjpgCaptureCamera: a camera on the network sending a stream of
// JPEG pictures over HTTP (an IP camera, a Raspberry Pi streamer) at
// "http://host:port/path". Each picture is taken from the stream as it comes
// (from its JPEG start to its end), the newest when several have come.
class JPMjpgSource : public JPCaptureSource {
public:
    JPMjpgSource(std::string name, std::string url, int width, int height, int timeoutMs);
    ~JPMjpgSource() override { close(); }

    bool open(std::string& error) override;
    void close() override;
    std::vector<JPCaptureMode> modes() const override;
    bool start(const JPCaptureMode& mode, std::string& error) override;
    bool grab(JPFrame& frame, int timeoutMs, std::string& error) override;
    std::string describe() const override { return m_name + " (" + m_url + ")"; }

    // The newest complete JPEG in `buffer` taken out (what is before it dropped); false while none is.
    static bool takeJpeg(std::vector<uint8_t>& buffer, std::vector<uint8_t>& jpeg);

private:
    std::string          m_name, m_url;
    int                  m_width, m_height, m_timeoutMs;
    int                  m_socket = -1;
    std::vector<uint8_t> m_buffer;
};

} // inline namespace jf
