// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPFootprint.h"

#include <optional>
#include <string>
#include <vector>

inline namespace jf {

// A part as EasyEDA's component lookup gives it, by LCSC's part number ("C14663"): its footprint's pads (the SMD
// pads on the top copper, in millimetres from the footprint's origin, Y up; rectangles square, ovals and circles
// round, a polygon by the rectangle round it; pad 1 marked), the footprint's name, and what it says of the part
// (its MPN, manufacturer, description, and its height where its 3D model's name gives one: "…-H0.8").
class JPEasyEdaPart {
public:
    static constexpr double kUnitMm = 0.254;   // EasyEDA's unit: 10 mil

    std::string                    lcsc, footprintName, mpn, manufacturer, description;
    std::optional<double>          heightMm;
    std::vector<JPFootprint::Pad>  pads;
    int                            polygons = 0;      // pads that were polygons, made rectangles
    int                            throughHole = 0;   // pads on every layer, left out

    // From the lookup's answer (its JSON). False (and why) when it names no part, or the part has no footprint.
    static bool parse(const std::string& json, JPEasyEdaPart& out, std::string& why);
    // LCSC's part number as typed ("c14663", " C14663 ", "14663") as LCSC writes it ("C14663"); empty: not one.
    static std::string partNumber(const std::string& typed);
    // Where it is looked up.
    static std::string url(const std::string& lcsc);
};

} // inline namespace jf
