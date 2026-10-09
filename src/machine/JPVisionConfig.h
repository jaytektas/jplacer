// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <j/config/Json.h>

#include <string>

inline namespace jf {

// The machine's vision, as OpenPnP's machine.xml has it: its bottom vision
// (<part-alignment class="ReferenceBottomVision">: on or off, the vision
// settings parts use unless they or their packages say otherwise, and its
// passes) and its fiducial locator (<fiducial-locator>: likewise). Kept in
// the cell; the settings themselves are the configuration's
// (vision-settings.xml).
struct JPVisionConfig {
    bool        bottomVisionEnabled = true;
    std::string bottomVisionId      = "BVS_Default";
    bool        preRotate           = true;
    int         maxVisionPasses     = 3;
    double      maxAngularOffset    = 10.0;   // degrees
    double      maxLinearOffsetMm   = 1.0;
    double      testAlignmentAngle  = 0.0;
    std::string fiducialVisionId    = "FVS_Default";
    bool        enabledAveraging    = false;
    // OpenPnP's ReferenceFiducialLocator.maxDistance: for pipelines without a maxDistance stage.
    double      fiducialMaxDistanceMm = 4.0;
    // A fiducial check sets each board's or panel's Z, measured by the head camera at its first fiducial
    // (JPJobMachine::fiducialHeight; jplacer's: OpenPnP keeps the Z it was given).
    bool        measureBoardZ = false;

    JJson toJson() const {
        JJson j = JJson::object();
        j["bottomVisionEnabled"] = bottomVisionEnabled;
        j["bottomVisionId"] = bottomVisionId;
        j["preRotate"] = preRotate;
        j["maxVisionPasses"] = maxVisionPasses;
        j["maxAngularOffset"] = maxAngularOffset;
        j["maxLinearOffsetMm"] = maxLinearOffsetMm;
        j["testAlignmentAngle"] = testAlignmentAngle;
        j["fiducialVisionId"] = fiducialVisionId;
        j["enabledAveraging"] = enabledAveraging;
        j["fiducialMaxDistanceMm"] = fiducialMaxDistanceMm;
        j["measureBoardZ"] = measureBoardZ;
        return j;
    }
    static JPVisionConfig fromJson(const JJson& j) {
        JPVisionConfig c;
        if (!j.isObject()) return c;
        if (j["bottomVisionEnabled"].isBool()) c.bottomVisionEnabled = j["bottomVisionEnabled"].boolean();
        if (j["bottomVisionId"].isString()) c.bottomVisionId = j["bottomVisionId"].str();
        if (j["preRotate"].isBool()) c.preRotate = j["preRotate"].boolean();
        if (j["maxVisionPasses"].isNumber()) c.maxVisionPasses = int(j["maxVisionPasses"].number());
        if (j["maxAngularOffset"].isNumber()) c.maxAngularOffset = j["maxAngularOffset"].number();
        if (j["maxLinearOffsetMm"].isNumber()) c.maxLinearOffsetMm = j["maxLinearOffsetMm"].number();
        if (j["testAlignmentAngle"].isNumber()) c.testAlignmentAngle = j["testAlignmentAngle"].number();
        if (j["fiducialVisionId"].isString()) c.fiducialVisionId = j["fiducialVisionId"].str();
        if (j["enabledAveraging"].isBool()) c.enabledAveraging = j["enabledAveraging"].boolean();
        if (j["fiducialMaxDistanceMm"].isNumber()) c.fiducialMaxDistanceMm = j["fiducialMaxDistanceMm"].number();
        if (j["measureBoardZ"].isBool()) c.measureBoardZ = j["measureBoardZ"].boolean();
        return c;
    }
};

} // inline namespace jf
