// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "camera/JPFrame.h"

#include <memory>

inline namespace jf {

// OpenPnP's HsvIndicator (a nozzle tip's Background Calibration): the HSV
// ranges a background calibration found, as a picture. A colour wheel (hue
// round it, saturation out from the middle), the colours inside the hue and
// saturation ranges bright and the rest dimmed to the least value; beside it
// a bar of value (bright at the top), the values outside the range faded, in
// the range's middle hue and its saturations across. All in 0..255, hue
// round the circle (its minimum above its maximum: the range through red).
// Disabled (the method not keying a colour), drawn without colour. Outside
// the wheel and the bar, transparent.
class JPHsvIndicator {
public:
    struct Ranges {
        int minHue = 0, maxHue = 0;
        int minSaturation = 0, maxSaturation = 0;
        int minValue = 0, maxValue = 0;
    };
    // OpenPnP's preferred size, in pixels.
    static constexpr int kWidth = 192, kHeight = 128;

    static std::shared_ptr<const JPFrame> picture(const Ranges& ranges, bool enabled, int width = kWidth, int height = kHeight);
};

} // inline namespace jf
