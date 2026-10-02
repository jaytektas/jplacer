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
    return j;
}

} // inline namespace jf
