// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPOrigin.h"
#include "JPPad.h"

#include <j/config/Json.h>

#include <string>
#include <vector>

inline namespace jf {

// A package's pads and body outline, as drawn over the camera's picture and
// looked for by vision: in millimetres, at the package's 0°, its centre at
// 0, 0. Pin 1 is the pad named by `pin1` (the dot drawn on the reticle).
// Many packages may use one footprint (an 0603 resistor and an 0603
// capacitor: one land pattern, two heights).
struct JPFootprint {
    std::string        id;
    std::string        name;
    std::vector<JPPad> pads;
    double             bodyWidth = 0, bodyLength = 0;   // along X and Y
    std::string        pin1;
    int                revision = 1;   // raised by every edit
    JPOrigin           origin;

    const JPPad* pad(const std::string& padName) const;
    const JPPad* pin1Pad() const { return pin1.empty() ? nullptr : pad(pin1); }

    static JPFootprint fromJson(const JJson& j);
    JJson toJson() const;
};

} // inline namespace jf
