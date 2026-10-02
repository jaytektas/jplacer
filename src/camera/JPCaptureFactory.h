// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPCaptureSource.h"

#include <memory>
#include <optional>
#include <vector>
#include <string>

inline namespace jf {

class JJson;

// Builds the capture source a camera's configuration names:
//   { "backend": "v4l2", "name": "top: top" }
//   { "backend": "simulated", "width": 640, "height": 480, "fps": 10 }
// plus, for either, an optional chosen mode: "format", "width", "height", "fps".
class JPCaptureFactory {
public:
    static std::unique_ptr<JPCaptureSource> create(const std::string& cameraName, const JJson& device,
                                                   std::string& error);

    // The mode to start in: the one the configuration names if the device
    // offers it, else the biggest picture in a format that decodes fastest
    // (MJPG before YUYV). Nothing when the device offers none.
    static std::optional<JPCaptureMode> choose(const std::vector<JPCaptureMode>& modes, const JJson& device);
};

} // inline namespace jf
