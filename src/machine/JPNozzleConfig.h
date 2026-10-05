// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPMachineLocation.h"
#include "JPMountConfig.h"

#include <string>
#include <vector>

inline namespace jf {

// A nozzle: where it rides, the actuators of its vacuum (empty when it has
// none), how long a pick and a place wait, the nozzle tips that fit it and the
// one on it now (empty: none, or not known).
struct JPNozzleConfig {
    std::string              id;
    std::string              name;
    JPMountConfig            mount;
    std::string              vacuumActuatorId;
    std::vector<std::string> tipIds;
    std::string              tipId;
    // A place blows the part off: this actuator, pulsed for the place dwell;
    // when it closes the vacuum valve itself, the vacuum is not switched off
    // first.
    std::string              blowOffActuatorId;
    bool                     blowOffClosesVacuum = false;
    // Reads the vacuum level (to tell a part is on); empty: the vacuum actuator's own read.
    std::string              vacuumSenseActuatorId;
    // Waited after the vacuum is on (pick) or off (place), with the tip's own.
    int                      pickDwellMs = 0;
    int                      placeDwellMs = 0;
    // OpenPnP's Dynamic Safe Z: carrying a part, its safe Z raised by the
    // part's height, so the part's bottom is at safe Z (within the safe zone).
    bool                     dynamicSafeZ = false;
    // OpenPnP's Tool Changer: tips changed by their load and unload steps
    // (else asked to be changed by hand, as for a tip without steps); and a
    // pick from the Feeders tab changing to a tip that fits the part.
    bool                     changerEnabled = true;
    bool                     tipChangeOnManualPick = false;
    // Where the nozzle goes for a tip to be changed by hand (none: where it is, at safe Z).
    std::optional<JPMachineLocation> manualChangeLocation;
    // OpenPnP's Rotation Mode (JPJobMachine::Nozzle::rotationMode), and for
    // LimitedArticulation how far it may turn about the pick and alignment.
    std::string              rotationMode = "AbsolutePartAngle";
    double                   maxPickArticulation = 15, maxAlignArticulation = 30;
    // Homing this nozzle's Z alone (Z only, from the park place): G-code
    // lines sent to the controller of the motor behind its Z, which ends
    // at the axis's home coordinate as a full home does. Empty: none.
    std::string              homeCommand;

    bool fits(const std::string& nozzleTipId) const {
        for (const std::string& t : tipIds) if (t == nozzleTipId) return true;
        return false;
    }

    static JPNozzleConfig fromJson(const JJson& j) {
        JPNozzleConfig n{ j["id"].str(), j["name"].str(), JPMountConfig::fromJson(j["mount"]), j["vacuumActuator"].str(), {},
                          j["tip"].str() };
        for (const JJson& t : j["tips"].arr()) n.tipIds.push_back(t.str());
        n.blowOffActuatorId     = j["blowOffActuator"].str();
        n.blowOffClosesVacuum   = j["blowOffClosesVacuum"].boolean();
        n.vacuumSenseActuatorId = j["vacuumSenseActuator"].str();
        n.pickDwellMs           = int(j["pickDwellMs"].number());
        n.dynamicSafeZ          = j["dynamicSafeZ"].boolean();
        n.changerEnabled        = j["changerEnabled"].boolean(true);
        n.tipChangeOnManualPick = j["tipChangeOnManualPick"].boolean();
        n.manualChangeLocation  = JPMachineLocation::fromJson(j["manualChangeLocation"]);
        if (!j["rotationMode"].str().empty()) n.rotationMode = j["rotationMode"].str();
        n.maxPickArticulation   = j["maxPickArticulation"].number(15.0);
        n.maxAlignArticulation  = j["maxAlignArticulation"].number(30.0);
        n.placeDwellMs          = int(j["placeDwellMs"].number());
        n.homeCommand           = j["homeCommand"].str();
        return n;
    }
    JJson toJson() const {
        JJson j = JJson::object();
        j["id"]             = id;
        j["name"]           = name;
        j["mount"]          = mount.toJson();
        j["vacuumActuator"] = vacuumActuatorId;
        JJson tips = JJson::array();
        for (const std::string& t : tipIds) tips.push(JJson(t));
        j["tips"]           = tips;
        j["tip"]            = tipId;
        if (!blowOffActuatorId.empty()) j["blowOffActuator"] = blowOffActuatorId;
        if (blowOffClosesVacuum) j["blowOffClosesVacuum"] = true;
        if (!vacuumSenseActuatorId.empty()) j["vacuumSenseActuator"] = vacuumSenseActuatorId;
        if (pickDwellMs) j["pickDwellMs"] = pickDwellMs;
        if (dynamicSafeZ) j["dynamicSafeZ"] = true;
        if (!changerEnabled) j["changerEnabled"] = false;
        if (tipChangeOnManualPick) j["tipChangeOnManualPick"] = true;
        if (manualChangeLocation) j["manualChangeLocation"] = manualChangeLocation->toJson();
        if (rotationMode != "AbsolutePartAngle") j["rotationMode"] = rotationMode;
        if (maxPickArticulation != 15) j["maxPickArticulation"] = maxPickArticulation;
        if (maxAlignArticulation != 30) j["maxAlignArticulation"] = maxAlignArticulation;
        if (placeDwellMs) j["placeDwellMs"] = placeDwellMs;
        if (!homeCommand.empty()) j["homeCommand"] = homeCommand;
        return j;
    }
};

} // inline namespace jf
