// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <string>
#include <variant>

inline namespace jf {

// A value a pipeline's caller sets on it (OpenPnP's pipeline properties),
// that a stage takes in place of its own setting ("MaskCircle.diameter"): a
// number, an integer, a flag, a text, or a length, area or machine place,
// which the stage turns into pixels through the camera.
struct JPPipelineValue {
    struct LengthMm {
        double mm = 0;
    };
    struct AreaMm2 {
        double mm2 = 0;
    };
    struct LocationMm {
        double x = 0, y = 0;
    };
    struct Pixel {
        double x = 0, y = 0;
    };
    std::variant<double, long, bool, std::string, LengthMm, AreaMm2, LocationMm, Pixel> value;
};

} // inline namespace jf
