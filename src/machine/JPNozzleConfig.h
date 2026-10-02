// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPMountConfig.h"

#include <string>

inline namespace jf {

// A nozzle: where it rides, and the actuator that switches and senses its
// vacuum (empty when it has none).
struct JPNozzleConfig {
    std::string   id;
    std::string   name;
    JPMountConfig mount;
    std::string   vacuumActuatorId;

    static JPNozzleConfig fromJson(const JJson& j) {
        return { j["id"].str(), j["name"].str(), JPMountConfig::fromJson(j["mount"]), j["vacuumActuator"].str() };
    }
    JJson toJson() const {
        JJson j = JJson::object();
        j["id"]             = id;
        j["name"]           = name;
        j["mount"]          = mount.toJson();
        j["vacuumActuator"] = vacuumActuatorId;
        return j;
    }
};

} // inline namespace jf
