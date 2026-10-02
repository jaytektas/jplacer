// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPHeadConfig.h"

inline namespace jf {

JPHeadConfig JPHeadConfig::fromJson(const JJson& j) {
    JPHeadConfig h;
    h.id                    = j["id"].str();
    h.name                  = j["name"].str();
    h.homingFiducial        = JPLocation::fromJson(j["homingFiducial"]);
    h.visualHoming          = j["visualHoming"].boolean();
    h.park                  = JPLocation::fromJson(j["park"]);
    const JJson& rig        = j["calibrationRig"];
    h.rigPrimary            = JPLocation::fromJson(rig["primary"]);
    h.rigSecondary          = JPLocation::fromJson(rig["secondary"]);
    h.rigPrimaryDiameter    = rig["primaryDiameter"].number();
    h.rigSecondaryDiameter  = rig["secondaryDiameter"].number();
    h.rigTestObjectDiameter = rig["testObjectDiameter"].number();
    h.pumpActuatorId        = j["pump"]["actuator"].str();
    h.pumpControl           = j["pump"]["control"].str();
    h.pumpOnWaitMs          = int(j["pump"]["onWaitMs"].number());
    return h;
}

JJson JPHeadConfig::toJson() const {
    JJson j = JJson::object();
    j["id"]   = id;
    j["name"] = name;
    if (homingFiducial) j["homingFiducial"] = homingFiducial->toJson();
    j["visualHoming"] = visualHoming;
    if (park) j["park"] = park->toJson();
    if (rigPrimary || rigSecondary) {
        JJson rig = JJson::object();
        if (rigPrimary)   rig["primary"]   = rigPrimary->toJson();
        if (rigSecondary) rig["secondary"] = rigSecondary->toJson();
        rig["primaryDiameter"]    = rigPrimaryDiameter;
        rig["secondaryDiameter"]  = rigSecondaryDiameter;
        rig["testObjectDiameter"] = rigTestObjectDiameter;
        j["calibrationRig"] = rig;
    }
    if (!pumpActuatorId.empty()) {
        j["pump"]["actuator"] = pumpActuatorId;
        j["pump"]["control"]  = pumpControl;
        j["pump"]["onWaitMs"] = pumpOnWaitMs;
    }
    return j;
}

} // inline namespace jf
