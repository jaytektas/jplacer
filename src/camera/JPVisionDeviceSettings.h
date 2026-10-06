// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <j/config/Json.h>

#include <optional>
#include <string>

inline namespace jf {

// What computer vision wants of a capture device's own settings, as OpenPnP's
// CameraSolutions says: "raw information from the camera sensor, even if the
// images look less appealing to humans". Brightness, contrast, gamma, gain,
// hue, saturation and white balance at the device's default (white balance
// then done by jplacer's own, as OpenPnP proposes), sharpness at its least,
// none of them automatic. Exposure is not one: it is kept, by hand, at what
// suits what the camera sees. Shared by Issues & Solutions and Auto-Tune.
class JPVisionDeviceSettings {
public:
    // Whether `key` (a device setting, as JPCaptureSource names it) is one of them.
    static bool isOne(const std::string& key);
    // At its least, rather than its default.
    static bool atMinimum(const std::string& key);
    // The value wanted for it, from what the device says of it (its "min" or "default"); none when it does not say.
    static std::optional<double> wanted(const std::string& key, const JJson& control);
};

} // inline namespace jf
