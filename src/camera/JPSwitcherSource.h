// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPCaptureSource.h"

#include <functional>
#include <string>

inline namespace jf {

class JPCameraFeed;

// OpenPnP's SwitcherCamera: one of several analog cameras on one capture
// device through a multiplexer, switched by an actuator. Its pictures are the
// device camera's, as taken (each switcher camera has its own calibration,
// transforms and white balance). Cameras sharing a switcher number share the
// multiplexer: a camera wanted (claimed: vision about to take its picture, or
// it just started showing) while another is switched in sets the actuator to
// its value and waits the actuator delay; one not switched in waits idle,
// keeping its last picture.
class JPSwitcherSource : public JPCaptureSource {
public:
    struct Settings {
        std::string cameraId;      // the capture device's camera
        int         switcher = 0;  // which multiplexer
        std::string actuatorId;    // what switches it
        double      actuatorValue = 0;
        int         delayMs = 500; // after switching, before its pictures are its own
    };
    // What it needs of the rest: another camera's feed (started if it is not
    // running), and an actuator set (false with why). `takeClaim`: whether it
    // is wanted now (cleared on reading). And another camera's device
    // settings, as configured (a NeoDen 4 switcher camera's source's).
    struct Links {
        std::function<JPCameraFeed*(const std::string& cameraId)> camera;
        std::function<bool(const std::string& actuatorId, double value, std::string& why)> actuate;
        std::function<JJson(const std::string& cameraId)> deviceOf;
    };

    JPSwitcherSource(std::string cameraName, Settings settings, Links links, std::function<bool()> takeClaim);

    bool open(std::string& error) override;
    void close() override {}
    std::vector<JPCaptureMode> modes() const override;
    bool start(const JPCaptureMode&, std::string&) override { return true; }
    bool grab(JPFrame& frame, int timeoutMs, std::string& error) override;
    bool idle() const override { return m_idle; }
    std::string describe() const override;

private:
    std::string           m_name;
    Settings              m_settings;
    Links                 m_links;
    std::function<bool()> m_takeClaim;
    JPCameraFeed*         m_device = nullptr;
    uint64_t              m_have = 0;
    bool                  m_idle = false;
};

} // inline namespace jf
