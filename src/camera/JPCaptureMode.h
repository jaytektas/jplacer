// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <string>

inline namespace jf {

// One way a camera can deliver pictures: its pixel format as the device
// names it (MJPG, YUYV…), the size, and the frame rate.
struct JPCaptureMode {
    std::string format;
    int         width  = 0;
    int         height = 0;
    double      fps    = 0;

    // As OpenPnP's Format list says it (CaptureFormat.toString): "1280 x 720, 30 FPS, MJPG".
    std::string openpnpText() const {
        return std::to_string(width) + " x " + std::to_string(height) + ", " + std::to_string(int(fps + 0.5)) + " FPS, " + format;
    }

    std::string describe() const {
        return format + " " + std::to_string(width) + "\xC3\x97" + std::to_string(height) + " @ "
             + std::to_string(int(fps + 0.5)) + " fps";
    }
};

} // inline namespace jf
