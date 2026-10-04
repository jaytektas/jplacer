// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPMachineLocation.h"

#include <optional>
#include <string>

inline namespace jf {

// A head: the moving carriage nozzles, cameras and actuators are mounted on
// (JPMountConfig::headId names it), and the places and parts that belong to it.
struct JPHeadConfig {
    std::string id;
    std::string name;

    // VISUAL HOMING: after homing on the switches, the head camera looks for
    // the homing fiducial, a small round mark at a known place, and the axes
    // are reset so that place is where it really is.
    std::optional<JPMachineLocation> homingFiducial;
    double                    homingFiducialDiameter = 0;
    bool                      visualHoming = false;
    // Where Park takes the head.
    std::optional<JPMachineLocation> park;
    // The calibration rig: two round fiducials at two heights (the camera
    // calibration measures the camera in 3D from them), and a test object.
    std::optional<JPMachineLocation> rigPrimary, rigSecondary;
    double rigPrimaryDiameter = 0, rigSecondaryDiameter = 0, rigTestObjectDiameter = 0;
    // The vacuum pump: its actuator, when it runs, how long it takes to come up.
    std::string pumpActuatorId;
    std::string pumpControl;
    int         pumpOnWaitMs = 0;

    static JPHeadConfig fromJson(const JJson& j);
    JJson toJson() const;
};

} // inline namespace jf
