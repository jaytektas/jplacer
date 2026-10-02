// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <j/config/Json.h>

#include <string>

inline namespace jf {

// Where something rides: the head it is mounted on (empty: fixed to the
// machine), the axes that move it, and its offset from the head's reference
// point in mm. Shared by nozzles, cameras and actuators.
struct JPMountConfig {
    std::string headId;
    std::string axisX, axisY, axisZ, axisRotation;
    double offsetX = 0, offsetY = 0, offsetZ = 0;

    static JPMountConfig fromJson(const JJson& j);
    JJson toJson() const;
};

} // inline namespace jf
