// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <string>
#include <variant>
#include <vector>

inline namespace jf {

// A value a pipeline's caller sets on it (OpenPnP's pipeline properties),
// that a stage takes in place of its own setting ("MaskCircle.diameter"): a
// number, an integer, a flag, a text, or a length, area or machine place,
// which the stage turns into pixels through the camera; or a shape, a
// footprint or a part, in millimetres, for the stages that draw templates.
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
    // A closed outline, the Y axis up (OpenPnP's Shape).
    using Outline = std::vector<LocationMm>;
    struct Shape {
        std::vector<Outline> outlines;
    };
    // A package's footprint (OpenPnP's Footprint): pads and body.
    struct Footprint {
        std::vector<Outline> pads;
        Outline              body;
    };
    // The part the pipeline looks at (or the feeder's): what its template
    // files are named after, and its package's body.
    struct Part {
        std::string id, packageId;
        bool        hasFootprint = false;
        double      bodyWidthMm = 0, bodyHeightMm = 0;
    };
    std::variant<double, long, bool, std::string, LengthMm, AreaMm2, LocationMm, Pixel, Shape, Footprint, Part> value;
};

} // inline namespace jf
