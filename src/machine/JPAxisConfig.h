// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPBacklashCalibration.h"

#include <j/config/Json.h>

#include <optional>
#include <string>
#include <utility>
#include <vector>

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
    // BACKLASH: the play in the drive.
    //  - OneSided: every move ends the same way: to the target plus the
    //    offset first, then in to the target at backlashSpeedFactor of its
    //    speed, so the last stretch is always the offset long and travels
    //    against its sign. The offset need only be at least the play; the end
    //    position is then the same whichever way, and however far, the axis
    //    came.
    //  - OneSidedOptimized: the same, but a move already arriving the right
    //    way goes straight in (its last stretch as long as the move).
    //  - Directional: a move travelling the way the offset points goes the
    //    offset further, taking up the play; the offset must be the play.
    //  - DirectionalSneakUp: the same, the last sneakUpMm of the move made at
    //    backlashSpeedFactor of its speed, so it cannot overshoot.
    //  - DistanceAware (jplacer's own): the drive lags the place it is sent
    //    to by an amount that depends on how far it has travelled since it last
    //    changed direction (a gap taken up first, then a belt winding up):
    //    `backlashTable`, (travel, lag) measured by Calibrate. Each move is
    //    sent the lag further, for the travel it will have made; one that
    //    would arrive having travelled less than `approachMm` since turning
    //    first backs off, so it comes in at least that far.
    enum class Backlash { None, OneSided, OneSidedOptimized, Directional, DirectionalSneakUp, DistanceAware };
    Backlash backlash = Backlash::None;
    double backlashOffset = 0;
    double backlashSpeedFactor = 1;
    double sneakUpMm = 0;
    std::vector<std::pair<double, double>> backlashTable;   // DistanceAware: (travel, lag), travel rising
    double approachMm = 0;                                  // DistanceAware: the least travel coming in
    // DistanceAware: the lag after travelling `travel` since turning (the
    // table, between its points on a log scale of travel, its ends beyond).
    double lagAfter(double travel) const;
    // The last time the backlash was measured (Calibrate on its Backlash tab).
    std::optional<JPBacklashCalibration> backlashCalibration;
    // Backlash's names, as kept and as OpenPnP calls them.
    static const char* backlashWord(Backlash b);
    static Backlash backlashFromWord(const std::string& w);
    double feedratePerSecond = 0, accelerationPerSecond2 = 0, jerkPerSecond3 = 0;
    bool   wrapAroundRotation = false, limitRotation = false;
    // What one motor step moves the axis (mm or degrees; 0: not known): a
    // move goes to the nearest whole step.
    double resolution = 0;

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
