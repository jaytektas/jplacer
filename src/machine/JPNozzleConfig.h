// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPMountConfig.h"

#include <string>
#include <vector>

inline namespace jf {

// A nozzle: where it rides, the actuator that switches and senses its vacuum
// (empty when it has none), the nozzle tips that fit it and the one on it now
// (empty: none, or not known).
struct JPNozzleConfig {
    std::string              id;
    std::string              name;
    JPMountConfig            mount;
    std::string              vacuumActuatorId;
    std::vector<std::string> tipIds;
    std::string              tipId;

    bool fits(const std::string& nozzleTipId) const {
        for (const std::string& t : tipIds) if (t == nozzleTipId) return true;
        return false;
    }

    static JPNozzleConfig fromJson(const JJson& j) {
        JPNozzleConfig n{ j["id"].str(), j["name"].str(), JPMountConfig::fromJson(j["mount"]), j["vacuumActuator"].str(), {},
                          j["tip"].str() };
        for (const JJson& t : j["tips"].arr()) n.tipIds.push_back(t.str());
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
        return j;
    }
};

} // inline namespace jf
