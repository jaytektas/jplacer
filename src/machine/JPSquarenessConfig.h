// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <j/config/Json.h>

#include <string>

inline namespace jf {

// A gantry whose Y axis is not quite square to its X: moving along Y also
// carries the head a little along X. `xPerY` is that drift (mm of X per mm
// of Y). jplacer's coordinates are square; the axes' own are leaned:
//   square X = axis X + xPerY * (axis Y - atY),   square Y = axis Y.
// At Y `atY` the two agree (the head's homing mark's Y, so the coordinates
// the homing mark gives are unchanged). Measured from a board's fiducials (a
// board is square); none when the axes are not named.
struct JPSquarenessConfig {
    std::string axisX, axisY;
    double      xPerY = 0;
    double      atY = 0;

    bool active() const { return !axisX.empty() && !axisY.empty() && xPerY != 0; }

    static JPSquarenessConfig fromJson(const JJson& j) {
        JPSquarenessConfig s;
        s.axisX = j["axisX"].str();
        s.axisY = j["axisY"].str();
        s.xPerY = j["xPerY"].number(0.0);
        s.atY   = j["atY"].number(0.0);
        return s;
    }
    JJson toJson() const {
        JJson j = JJson::object();
        j["axisX"] = axisX;
        j["axisY"] = axisY;
        j["xPerY"] = xPerY;
        j["atY"]   = atY;
        return j;
    }
};

} // inline namespace jf
