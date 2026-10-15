// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <j/config/Json.h>

#include <string>

inline namespace jf {

// Where something rides: the head it is mounted on (empty: fixed to the
// machine), the axes that move it, and its offset from the head's reference
// point in mm. Shared by nozzles, cameras and actuators.
//
// A nozzle's X, Y offsets hold at Z `leanZ`; its lean (calibrated at two heights) is how far its tip moves in X
// and Y for each mm its Z goes up: a Z axis not quite square to the table. At Z z the offsets are
//   offsetX + leanX * (z - leanZ),   offsetY + leanY * (z - leanZ)
// (no lean: offsetX, offsetY at any height).
struct JPMountConfig {
    std::string headId;
    std::string axisX, axisY, axisZ, axisRotation;
    double offsetX = 0, offsetY = 0, offsetZ = 0;
    double leanX = 0, leanY = 0, leanZ = 0;

    bool   leans() const { return leanX != 0 || leanY != 0; }
    double offsetXAt(double z) const { return offsetX + leanX * (z - leanZ); }
    double offsetYAt(double z) const { return offsetY + leanY * (z - leanZ); }

    static JPMountConfig fromJson(const JJson& j);
    JJson toJson() const;
};

} // inline namespace jf
