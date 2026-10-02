// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <j/config/Json.h>

#include <optional>
#include <string>

inline namespace jf {

// One axis of a cell. Lengths in mm, rotations in degrees, rates per second.
//
//  - CONTROLLER: driven by a controller's axis letter.
//  - VIRTUAL:    no hardware; holds the coordinate it was last given (e.g. a
//                camera's Z, so the camera has a full location).
//  - MAPPED:     follows another axis through a straight-line map given by
//                two points; e.g. two nozzles sharing one Z, one of them
//                inverted (input -1 -> output 1).
//
// Feed rate, acceleration and jerk are 0 when the controller's own stored
// values apply; jplacer keeps only what the controller does not hold.
struct JPAxisConfig {
    enum class Kind { Controller, Virtual, Mapped };
    enum class Type { X, Y, Z, Rotation };

    std::string id;
    std::string name;
    Kind kind = Kind::Controller;
    Type type = Type::X;

    std::string driverId;
    std::string letter;
    double homeCoordinate = 0;
    double softLimitLow = 0, softLimitHigh = 0;
    bool   softLimitLowEnabled = false, softLimitHighEnabled = false;
    double safeZoneLow = 0, safeZoneHigh = 0;
    bool   safeZoneLowEnabled = false, safeZoneHighEnabled = false;
    double backlashOffset = 0;
    double feedratePerSecond = 0, accelerationPerSecond2 = 0, jerkPerSecond3 = 0;
    bool   wrapAroundRotation = false, limitRotation = false;

    std::string inputAxisId;
    double mapInput0 = 0, mapOutput0 = 0, mapInput1 = 1, mapOutput1 = 1;

    // A mapped axis's coordinate for its input axis at `input`, and back.
    // Nothing when the two map points share an input (no line through them).
    std::optional<double> mapped(double input) const;
    std::optional<double> unmapped(double output) const;

    static const char* kindName(Kind k);
    static const char* typeName(Type t);

    // Nothing, with `error`, when the kind or type is unknown or a required
    // field is missing.
    static std::optional<JPAxisConfig> fromJson(const JJson& j, std::string& error);
    JJson toJson() const;
};

} // inline namespace jf
