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
//    (set on Accept), a nozzle turned through less than 360° not placing
//    by Limited Articulation, or bottom vision not pre-rotating for it;
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
        // A camera calibrated, an X or Y axis's backlash calibrated (on the
        // machine, in the background): `finished` says whether it was.
        std::function<void(const std::string& cameraId, std::function<void(bool ok)> finished)> calibrateCamera;
        std::function<void(const std::string& axisId, std::function<void(bool ok)> finished)> calibrateBacklash;
        // A nozzle tip calibrated on the nozzle it is loaded on (runout, background).
        std::function<void(const std::string& tipId, std::function<void(bool ok)> finished)> calibrateTip;
        // The mark under a head's camera made its homing mark, visual homing on.
        std::function<void(const std::string& headId, std::function<void(bool ok)> finished)> enableVisualHoming;
        // OpenPnP's primary calibration fiducial: the head's camera calibrated over it, its place and size kept.
        std::function<void(const std::string& headId, std::function<void(bool ok)> finished)> capturePrimaryFiducial;
        // A camera's own device settings as it last started (JPCaptureSource::controls).
        std::function<JJson(const std::string& cameraId)> cameraControls;
        // A camera's picture drawn smoothed (Rendering Quality High or better), and set so or back to Low.
        std::function<bool(const std::string& cameraId)> renderingSmooth;
        std::function<void(const std::string& cameraId, bool smooth)> setRenderingSmooth;
        // View > Selections in Tables: Linked or not, and set so.
        std::function<bool()> tablesLinked;
        std::function<void(bool linked)> setTablesLinked;
        // What a controller said when it was identified (its M115 reply; empty: not connected).
        std::function<std::string(const std::string& driverId)> firmwareIdentity;
        // The firmware profile a controller was identified by (its name; empty: not connected).
        std::function<std::string(const std::string& driverId)> firmwareProfile;
        // `config` changed (saved, and the tabs showing it told).
        std::function<void()> configurationChanged;
        // The nozzle chosen on the Jog panel; a camera's view brought to the front.
        std::function<void(const std::string& nozzleId)> chooseNozzle;
        std::function<void(const std::string& cameraId)> showCamera;
        // OpenPnP's VisionFeatureIssue with the head camera: the feature of `px` shown on its view, Auto-Detect Next
        // (`done`: the diameter found), and its pixels a mm (none: not calibrated).
        std::function<void(int px)> previewFeature;
        std::function<void(int fromPx, std::function<void(std::optional<int>)> done)> autoDetectFeature;
        std::function<std::optional<double>()> headCameraPixelsPerMm;
        // OpenPnP's precise nozzle offsets: the test object measured at `px` and kept, then picked, turned and placed.
        std::function<void(const std::string& nozzleId, int px, std::function<void(bool ok)> finished)> calibratePreciseNozzleOffsets;
        struct OffsetsResult {
            double beforeX = 0, beforeY = 0, afterX = 0, afterY = 0;
        };
        std::function<std::optional<OffsetsResult>(const std::string& nozzleId)> nozzleOffsetsResult;
        // Where a nozzle's Z is now, as a pick at a Z takes it (none: not known).
        std::function<std::optional<double>(const std::string& nozzleId)> nozzleZ;
        // A change to the cell's settings, a Machine Setup step (undone as one).
        std::function<void(const std::string& what, const std::function<void(JPCellConfig&)>& edit)> changeCell;
    };

    static std::vector<JPSolutions::Check> all(const Context& context);
};

} // inline namespace jf
