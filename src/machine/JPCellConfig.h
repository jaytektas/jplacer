// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPActuatorConfig.h"
#include "JPAxisConfig.h"
#include "JPCameraConfig.h"
#include "JPDriverConfig.h"
#include "JPHeadConfig.h"
#include "JPJobProcessorConfig.h"
#include "JPMachineLocation.h"
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
    JPSquarenessConfig            squareness;   // the gantry's Y lean, when measured
    // Home as soon as connected: any controller saying so (JPDriverConfig).
    bool homeAfterConnect() const {
        for (const JPDriverConfig& d : drivers)
            if (d.homeAfterConnect) return true;
        return false;
    }
    bool                          parkAfterHome    = false;   // park once homed (after visual homing)
    std::optional<JPMachineLocation>     discardLocation;            // where a part not wanted is dropped
    JPJobProcessorConfig          jobProcessor;                 // how a job is run

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
};

} // inline namespace jf
