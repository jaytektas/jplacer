// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPPad.h"

inline namespace jf {

JPPad JPPad::fromJson(const JJson& j) {
    JPPad p;
    p.name        = j["name"].str();
    p.x           = j["x"].number();
    p.y           = j["y"].number();
    p.width       = j["width"].number();
    p.height      = j["height"].number();
    p.rotationDeg = j["rotation"].number();
    p.roundness   = j["roundness"].number();
    return p;
}

JJson JPPad::toJson() const {
    JJson j = JJson::object();
    j["name"]      = name;
    j["x"]         = x;
    j["y"]         = y;
    j["width"]     = width;
    j["height"]    = height;
    j["rotation"]  = rotationDeg;
    j["roundness"] = roundness;
    return j;
}

} // inline namespace jf
