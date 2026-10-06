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
    // OpenPnP's Align with Part (aligning rotation mode): bottom vision's turn
    // of the part taken into the nozzle's rotation mode offset, so its
    // rotation reads the part's angle as aligned (JPCell::setRotationModeOffset).
    bool                     alignRotationWithPart = false;
    // Homing this nozzle's Z alone (Z only, from the park place): G-code
    // lines sent to the controller of the motor behind its Z, which ends
    // at the axis's home coordinate as a full home does. Empty: none.
    std::string              homeCommand;
    // OpenPnP's ContactProbeNozzle: the heights of feeders and placements (and
    // a part's height, when not known) found by touch. Method "None",
    // "ContactSenseActuator" (the actuator, switched on, probes down until the
    // controller senses contact, and off retracts; the place is where the
    // controller says it stopped) or "VacuumSense" (the nozzle stepped down
    // Sniffle Increment at a time until its part-off check senses the nozzle
    // blocked). From Start Offset above where it should be, as deep as Probe
    // Depth, then Final Adjustment (up: overshoot; down: spring tension). The
    // feeder and placement probing triggers: "Off", "Once", "AfterHoming" or
    // "EachTime"; Discard Probing probes the discard place each time.
    struct ContactProbe {
        bool        nozzle = false;   // the nozzle is a ContactProbeNozzle (else a ReferenceNozzle)
        std::string method = "None";
        std::string actuatorId;
        double      speed = 0.05;   // for the probing command (a share of top speed)
        double      startOffsetMm = 1, depthMm = 2, sniffleIncrementMm = 0.1, adjustMm = 0;
        int         sniffleDwellMs = 250;
        std::string feederHeightProbing = "EachTime", partHeightProbing = "EachTime";
        bool        discardProbing = false;
        double      maxZOffsetMm = 2;
        bool on() const { return nozzle && method != "None"; }
    };
    ContactProbe             contactProbe;
    std::string className() const { return contactProbe.nozzle ? "ContactProbeNozzle" : "ReferenceNozzle"; }

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
        n.alignRotationWithPart = j["alignRotationWithPart"].boolean();
        n.placeDwellMs          = int(j["placeDwellMs"].number());
        n.homeCommand           = j["homeCommand"].str();
        if (const JJson& c = j["contactProbe"]; c.isObject()) {
            ContactProbe& p = n.contactProbe;
            p.nozzle = true;
            if (!c["method"].str().empty()) p.method = c["method"].str();
            p.actuatorId         = c["actuator"].str();
            p.speed              = c["speed"].number(p.speed);
            p.startOffsetMm      = c["startOffset"].number(p.startOffsetMm);
            p.depthMm            = c["depth"].number(p.depthMm);
            p.sniffleIncrementMm = c["sniffleIncrement"].number(p.sniffleIncrementMm);
            p.adjustMm           = c["adjust"].number(p.adjustMm);
            p.sniffleDwellMs     = int(c["sniffleDwellMs"].number(p.sniffleDwellMs));
            if (!c["feederHeightProbing"].str().empty()) p.feederHeightProbing = c["feederHeightProbing"].str();
            if (!c["partHeightProbing"].str().empty()) p.partHeightProbing = c["partHeightProbing"].str();
            p.discardProbing     = c["discardProbing"].boolean(false);
            p.maxZOffsetMm       = c["maxZOffset"].number(p.maxZOffsetMm);
        }
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
        if (alignRotationWithPart) j["alignRotationWithPart"] = true;
        if (placeDwellMs) j["placeDwellMs"] = placeDwellMs;
        if (!homeCommand.empty()) j["homeCommand"] = homeCommand;
        if (contactProbe.nozzle) {
            const ContactProbe& p = contactProbe;
            JJson c = JJson::object();
            c["method"] = p.method;
            c["actuator"] = p.actuatorId;
            c["speed"] = p.speed;
            c["startOffset"] = p.startOffsetMm;
            c["depth"] = p.depthMm;
            c["sniffleIncrement"] = p.sniffleIncrementMm;
            c["adjust"] = p.adjustMm;
            c["sniffleDwellMs"] = p.sniffleDwellMs;
            c["feederHeightProbing"] = p.feederHeightProbing;
            c["partHeightProbing"] = p.partHeightProbing;
            c["discardProbing"] = p.discardProbing;
            c["maxZOffset"] = p.maxZOffsetMm;
            j["contactProbe"] = c;
        }
        return j;
    }
};

} // inline namespace jf
