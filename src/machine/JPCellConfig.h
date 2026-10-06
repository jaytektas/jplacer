// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPActuatorConfig.h"
#include "JPAxisConfig.h"
#include "JPCameraConfig.h"
#include "JPDriverConfig.h"
#include "JPHeadConfig.h"
#include "JPJobProcessorConfig.h"
#include "JPSignalerConfig.h"
#include "JPMachineLocation.h"
#include "JPMotionPlannerConfig.h"
#include "JPSimulationConfig.h"
#include "JPVisionConfig.h"
#include "JPNozzleConfig.h"
#include "JPNozzleTipConfig.h"
#include "JPSquarenessConfig.h"

#include <optional>
#include <string>
#include <vector>

inline namespace jf {

// A cell's configuration: one machine as `cells/<name>.json` describes it.
// Plain data; JPCell runs it.
struct JPCellConfig {
    std::string                   name;
    std::vector<JPDriverConfig>   drivers;
    std::vector<JPHeadConfig>     heads;
    std::vector<JPAxisConfig>     axes;
    std::vector<JPNozzleConfig>   nozzles;
    std::vector<JPNozzleTipConfig> nozzleTips;
    std::vector<JPCameraConfig>   cameras;
    std::vector<JPActuatorConfig> actuators;
    std::vector<JPSignalerConfig> signalers;   // OpenPnP's: told how a job runs
    JPSquarenessConfig            squareness;   // the gantry's Y lean, when measured
    bool                          homeAfterEnabled = false;   // OpenPnP's: home as soon as the machine is on
    bool                          parkAfterHome    = false;   // park once homed (after visual homing)
    std::optional<JPMachineLocation>     discardLocation;            // where a part not wanted is dropped
    JPMachineLocation             defaultBoardLocation;         // where a board or panel added to a job starts
    // A tool a panel moves (a camera taken to a feeder) is chosen on the Jog panel.
    bool                          autoToolSelect = true;
    // Z park (the Jog panel's) takes every tool on the head to safe Z first.
    bool                          safeZPark = true;
    // OpenPnP's Unsafe Z Roaming: a tool left below safe Z (a camera's
    // virtual Z, captured low) jogged further than this from where it was
    // left goes up to safe Z, so a later move does not bring it down unseen (mm).
    double                        unsafeZRoamingMm = 10;
    // The job open last is opened again at start.
    bool                          autoLoadMostRecentJob = true;
    // OpenPnP's Pool scripting engines?: Python and JavaScript interpreters kept for the next script (JPScripting).
    bool                          poolScriptingEngines = false;
    JPMotionPlannerConfig         motionPlanner;                // OpenPnP's motion planner: continuous motion, test motion
    JPSimulationConfig            simulation;                   // OpenPnP's Simulation Mode
    JPJobProcessorConfig          jobProcessor;                 // how a job is run
    JPVisionConfig                vision;                       // bottom vision and the fiducial locator

    // Read / write a cell file. False with `error` naming the file and problem.
    bool load(const std::string& path, std::string& error);
    bool save(const std::string& path, std::string& error) const;

    JJson toJson() const;
    bool fromJson(const JJson& j, std::string& error);

    // Every reference that points at nothing (an axis naming a missing
    // controller, a nozzle a missing axis, ...), in words; empty when sound.
    std::vector<std::string> problems() const;

    const JPAxisConfig*   axis(const std::string& id) const;
    const JPDriverConfig* driver(const std::string& id) const;
    // An actuator as OpenPnP finds one by name: on a head first, then on the
    // machine; else by id. None when there is no such actuator.
    const JPActuatorConfig* actuatorNamed(const std::string& name) const;
};

} // inline namespace jf
