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
    // The library part the homing fiducial is looked for as (its package's footprint, its fiducial vision), by
    // its id (a GUID: renaming it changes nothing); empty: the part named FIDUCIAL-HOME, as OpenPnP has it.
    std::string               homingFiducialPart;
    bool                      visualHoming = false;
    // Where Park takes the head.
    std::optional<JPMachineLocation> park;
    // The calibration rig: two round fiducials at two heights (the camera
    // calibration measures the camera in 3D from them), and a test object.
    std::optional<JPMachineLocation> rigPrimary, rigSecondary;
    double rigPrimaryDiameter = 0, rigSecondaryDiameter = 0, rigTestObjectDiameter = 0;
    // The test object the nozzles' precise offsets are calibrated with: it lies on the primary fiducial, its top
    // this far above it (0: paper-thin, as OpenPnP takes it). With two heights, the nozzle carries it to the
    // secondary fiducial and calibrates there too: the nozzle's lean (JPMountConfig) from the two.
    double rigTestObjectThickness = 0;
    bool   rigTwoHeights = false;
    // Where the test object is put for the precise offsets: on the primary (or secondary) fiducial, its top
    // (Z) the thickness above it; none without that fiducial.
    std::optional<JPMachineLocation> testObjectOn(bool secondary) const {
        const std::optional<JPMachineLocation>& f = secondary ? rigSecondary : rigPrimary;
        if (!f) return std::nullopt;
        return JPMachineLocation { f->x, f->y, f->z + rigTestObjectThickness, 0 };
    }
    // The vacuum pump: its actuator, when it runs, how long it takes to come up.
    std::string pumpActuatorId;
    std::string pumpControl;
    int         pumpOnWaitMs = 0;
    // OpenPnP's Z Probe actuator: read (in mm, from where it is) at a place the
    // camera captures, the place's Z.
    std::string zProbeActuatorId;

    static JPHeadConfig fromJson(const JJson& j);
    JJson toJson() const;
};

} // inline namespace jf
