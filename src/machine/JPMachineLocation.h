// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <j/config/Json.h>

#include <optional>

inline namespace jf {

// A place on the machine: X, Y, Z in mm and a rotation in degrees.
struct JPMachineLocation {
    double x = 0, y = 0, z = 0, rotation = 0;

    // Nothing when `j` is not an object (the place was never set).
    static std::optional<JPMachineLocation> fromJson(const JJson& j) {
        if (!j.isObject()) return std::nullopt;
        return JPMachineLocation{ j["x"].number(), j["y"].number(), j["z"].number(), j["rotation"].number() };
    }
    JJson toJson() const {
        JJson j = JJson::object();
        j["x"] = x;
        j["y"] = y;
        j["z"] = z;
        j["rotation"] = rotation;
        return j;
    }
};

} // inline namespace jf
