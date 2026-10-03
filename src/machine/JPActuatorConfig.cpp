// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPActuatorConfig.h"

inline namespace jf {

JPActuatorConfig JPActuatorConfig::fromJson(const JJson& j) {
    JPActuatorConfig a;
    a.id          = j["id"].str();
    a.name        = j["name"].str();
    a.driverId    = j["driver"].str();
    a.mount       = JPMountConfig::fromJson(j["mount"]);
    const std::string& type = j["valueType"].str();
    a.valueType   = type == "number" ? ValueType::Number : type == "text" ? ValueType::Text : ValueType::Boolean;
    a.index       = j["index"].str();
    a.onCommand   = j["onCommand"].str();
    a.offCommand  = j["offCommand"].str();
    a.readCommand = j["readCommand"].str();
    a.readPattern = j["readPattern"].str();
    a.unit        = j["unit"].str();
    // Left out: as an actuator OpenPnP makes is set.
    if (const std::string& v = j["enabledActuation"].str(); !v.empty()) a.enabledActuation = v;
    if (const std::string& v = j["homedActuation"].str(); !v.empty()) a.homedActuation = v;
    if (const std::string& v = j["disabledActuation"].str(); !v.empty()) a.disabledActuation = v;
    return a;
}

JJson JPActuatorConfig::toJson() const {
    JJson j = JJson::object();
    j["id"]          = id;
    j["name"]        = name;
    j["driver"]      = driverId;
    j["mount"]       = mount.toJson();
    j["valueType"]   = valueType == ValueType::Number ? "number" : valueType == ValueType::Text ? "text" : "boolean";
    j["index"]       = index;
    j["onCommand"]   = onCommand;
    j["offCommand"]  = offCommand;
    j["readCommand"] = readCommand;
    j["readPattern"] = readPattern;
    j["unit"]        = unit;
    j["enabledActuation"]  = enabledActuation;
    j["homedActuation"]    = homedActuation;
    j["disabledActuation"] = disabledActuation;
    return j;
}

} // inline namespace jf
