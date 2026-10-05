// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPCaptureSource.h"
#include "JPOnvif.h"

#include <chrono>
#include <string>
#include <vector>

inline namespace jf {

// OpenPnP's OnvifIPCamera: an IP camera set up over ONVIF (JPOnvif), its
// pictures its JPEG snapshots, fetched one after another (at most `fps` a
// second) and, given a target size, resized to it.
class JPOnvifSource : public JPCaptureSource {
public:
    struct Settings {
        std::string host, username, password;
        std::string preferredResolution;   // "WxH"; empty: the largest
        int         resizeWidth = 0, resizeHeight = 0;   // 0: as it comes
        double      fps = 10;
    };
    JPOnvifSource(std::string name, Settings settings);

    bool open(std::string& error) override;
    void close() override {}
    std::vector<JPCaptureMode> modes() const override;
    bool start(const JPCaptureMode&, std::string&) override { return true; }
    bool grab(JPFrame& frame, int timeoutMs, std::string& error) override;
    std::string describe() const override;

    // `rgba` (width x height) resized to w x h, each pixel the mean of what it covers.
    static void resize(const std::vector<uint8_t>& rgba, int width, int height, int w, int h, std::vector<uint8_t>& out);

private:
    std::string                           m_name;
    Settings                              m_settings;
    JPOnvif                               m_onvif;
    std::chrono::steady_clock::time_point m_last;
};

} // inline namespace jf
