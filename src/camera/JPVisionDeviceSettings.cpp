// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPVisionDeviceSettings.h"

#include <algorithm>
#include <array>

inline namespace jf {

namespace {

// OpenPnP's CameraSolutions, in its order.
constexpr std::array<const char*, 8> kSettings = { "brightness", "contrast", "gamma", "gain", "sharpness",
                                                   "hue", "saturation", "white-balance" };

} // namespace

bool JPVisionDeviceSettings::isOne(const std::string& key) {
    return std::any_of(kSettings.begin(), kSettings.end(), [&key](const char* k) { return key == k; });
}

bool JPVisionDeviceSettings::atMinimum(const std::string& key) {
    return key == "sharpness";
}

std::optional<double> JPVisionDeviceSettings::wanted(const std::string& key, const JJson& control) {
    const JJson& v = control[atMinimum(key) ? "min" : "default"];
    if (!isOne(key) || !v.isNumber()) return std::nullopt;
    return v.number();
}

} // inline namespace jf
