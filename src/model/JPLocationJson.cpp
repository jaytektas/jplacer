// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPLocationJson.h"

#include "JPLengthUnits.h"

inline namespace jf {

JJson JPLocationJson::to(const JPLocation& l) {
    JJson j = JJson::object();
    j["units"] = JPLengthUnits::name(l.units());
    j["x"] = l.x();
    j["y"] = l.y();
    j["z"] = l.z();
    j["rotation"] = l.rotation();
    return j;
}

JPLocation JPLocationJson::from(const JJson& j) {
    JPLengthUnit units = JPLengthUnit::Millimeters;
    if (j["units"].isString()) JPLengthUnits::fromName(j["units"].str(), units);
    return JPLocation(units, j["x"].number(), j["y"].number(), j["z"].number(), j["rotation"].number());
}

} // inline namespace jf
