// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPMountConfig.h"

inline namespace jf {

JPMountConfig JPMountConfig::fromJson(const JJson& j) {
    JPMountConfig m;
    m.headId       = j["head"].str();
    m.axisX        = j["axisX"].str();
    m.axisY        = j["axisY"].str();
    m.axisZ        = j["axisZ"].str();
    m.axisRotation = j["axisRotation"].str();
    m.offsetX      = j["offset"]["x"].number();
    m.offsetY      = j["offset"]["y"].number();
    m.offsetZ      = j["offset"]["z"].number();
    return m;
}

JJson JPMountConfig::toJson() const {
    JJson j = JJson::object();
    j["head"]         = headId;
    j["axisX"]        = axisX;
    j["axisY"]        = axisY;
    j["axisZ"]        = axisZ;
    j["axisRotation"] = axisRotation;
    j["offset"]["x"]  = offsetX;
    j["offset"]["y"]  = offsetY;
    j["offset"]["z"]  = offsetZ;
    return j;
}

} // inline namespace jf
