// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPSolutions.h"

#include "machine/JPCellConfig.h"
#include "model/JPConfiguration.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

inline namespace jf {

// What Issues & Solutions checks (JPSolutions), as OpenPnP's machine,
// head, camera, vision, nozzle tip and feeder checks, for jplacer's own
// Machine Setup where OpenPnP's checks its drivers' settings:
//  - always: Machine Setup's own problems (a part naming another not there);
//  - Welcome: a head without nozzles;
//  - Connect: a controller or camera still simulated;
//  - Basics: an axis without a controller or a letter (or E, or another
//    axis's on the same controller), a nozzle without a Z or rotation axis,
//    nozzles sharing one;
//  - Kinematics: the machine not homed (Home on Accept), a Z axis's safe
//    zone invalid or not set (captured on Accept), an X or Y axis without
//    soft limits (captured on Accept), an axis without a feed rate or
//    acceleration, a nozzle's rotation not wrapping around or not limited
//    (set on Accept);
//  - Vision: a camera settling by a fixed time (an adaptive method set on
//    Accept), one not calibrated, one without a white balance;
//  - Calibration: a nozzle tip no nozzle takes;
//  - Production: a Photon feeder's slot without a location, or without an
//    offset from it.
class JPIssueChecks {
public:
    struct Context {
        JPConfiguration* config = nullptr;
        // The cell's settings as they are now (none: no machine open).
        std::function<const JPCellConfig*()> cell;
        // Whether a camera is calibrated for the pictures it takes.
        std::function<bool(const std::string& cameraId)> calibrated;
        // Machine Setup's part selected ("camera:CAM1"; empty: none), for when its tab is opened.
        std::function<void(const std::string& path)> showSetup;
        // Whether the machine is homed; homing it; where an axis is now (none: not known).
        std::function<bool()> homed;
        std::function<void()> home;
        std::function<std::optional<double>(const std::string& axisId)> axisPosition;
        // A change to the cell's settings, a Machine Setup step (undone as one).
        std::function<void(const std::string& what, const std::function<void(JPCellConfig&)>& edit)> changeCell;
    };

    static std::vector<JPSolutions::Check> all(const Context& context);
};

} // inline namespace jf
