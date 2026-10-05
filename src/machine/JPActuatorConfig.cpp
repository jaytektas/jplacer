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
    a.valueType   = type == "number" ? ValueType::Number : type == "text" ? ValueType::Text
                  : type == "profile" ? ValueType::Profile : ValueType::Boolean;
    a.index       = j["index"].str();
    a.onCommand   = j["onCommand"].str();
    a.offCommand  = j["offCommand"].str();
    a.valueCommand = j["valueCommand"].str();
    a.onValue     = j["onValue"].str();
    a.offValue    = j["offValue"].str();
    a.readCommand = j["readCommand"].str();
    a.readPattern = j["readPattern"].str();
    a.unit        = j["unit"].str();
    // Left out: as an actuator OpenPnP makes is set.
    if (const std::string& v = j["enabledActuation"].str(); !v.empty()) a.enabledActuation = v;
    if (const std::string& v = j["homedActuation"].str(); !v.empty()) a.homedActuation = v;
    if (const std::string& v = j["disabledActuation"].str(); !v.empty()) a.disabledActuation = v;
    for (size_t k = 0; k < kProfileActuators; ++k) a.profileActuators[k] = j["profileActuators"][k].str();
    if (const JJson& il = j["interlock"]; il.isObject()) {
        Interlock& i = a.interlock;
        i.enabled = true;
        i.type = il["type"].str();
        if (i.type.empty()) i.type = "None";
        for (size_t k = 0; k < 4; ++k) i.axes[k] = il["axes"][k].str();
        i.conditionalActuatorId = il["conditionalActuator"].str();
        if (!il["conditionalState"].str().empty()) i.conditionalState = il["conditionalState"].str();
        i.speedMin = il["speedMin"].number(0.0);
        i.speedMax = il["speedMax"].number(1.0);
        i.goodMin = il["goodMin"].number(0.0);
        i.goodMax = il["goodMax"].number(0.0);
        i.pattern = il["pattern"].str();
        i.byRegex = il["byRegex"].boolean();
    }
    for (const JJson& p : j["profiles"].arr()) {
        Profile q;
        q.name = p["name"].str();
        q.defaultOn = p["defaultOn"].boolean();
        q.defaultOff = p["defaultOff"].boolean();
        for (size_t k = 0; k < kProfileActuators; ++k) q.values[k] = p["values"][k].str();
        a.profiles.push_back(std::move(q));
    }
    return a;
}

JJson JPActuatorConfig::toJson() const {
    JJson j = JJson::object();
    j["id"]          = id;
    j["name"]        = name;
    j["driver"]      = driverId;
    j["mount"]       = mount.toJson();
    j["valueType"]   = valueType == ValueType::Number ? "number" : valueType == ValueType::Text ? "text"
                     : valueType == ValueType::Profile ? "profile" : "boolean";
    j["index"]       = index;
    j["onCommand"]   = onCommand;
    j["offCommand"]  = offCommand;
    if (!valueCommand.empty()) j["valueCommand"] = valueCommand;
    if (!onValue.empty()) j["onValue"] = onValue;
    if (!offValue.empty()) j["offValue"] = offValue;
    j["readCommand"] = readCommand;
    j["readPattern"] = readPattern;
    j["unit"]        = unit;
    j["enabledActuation"]  = enabledActuation;
    j["homedActuation"]    = homedActuation;
    j["disabledActuation"] = disabledActuation;
    if (interlock.enabled) {
        JJson il = JJson::object();
        il["type"] = interlock.type;
        il["axes"] = JJson::array();
        for (const std::string& id : interlock.axes) il["axes"].push(JJson(id));
        if (!interlock.conditionalActuatorId.empty()) il["conditionalActuator"] = interlock.conditionalActuatorId;
        il["conditionalState"] = interlock.conditionalState;
        il["speedMin"] = interlock.speedMin;
        il["speedMax"] = interlock.speedMax;
        il["goodMin"] = interlock.goodMin;
        il["goodMax"] = interlock.goodMax;
        if (!interlock.pattern.empty()) il["pattern"] = interlock.pattern;
        if (interlock.byRegex) il["byRegex"] = true;
        j["interlock"] = il;
    }
    if (valueType == ValueType::Profile) {
        j["profileActuators"] = JJson::array();
        for (const std::string& id : profileActuators) j["profileActuators"].push(JJson(id));
        j["profiles"] = JJson::array();
        for (const Profile& p : profiles) {
            JJson q = JJson::object();
            q["name"] = p.name;
            if (p.defaultOn) q["defaultOn"] = true;
            if (p.defaultOff) q["defaultOff"] = true;
            q["values"] = JJson::array();
            for (const std::string& v : p.values) q["values"].push(JJson(v));
            j["profiles"].push(q);
        }
    }
    return j;
}

const std::vector<std::string>& JPActuatorConfig::Interlock::types() {
    static const std::vector<std::string> t { "None", "SignalAxesMoving", "SignalAxesStandingStill", "SignalAxesInsideSafeZone",
                                              "SignalAxesOutsideSafeZone", "SignalAxesParked", "SignalAxesUnparked",
                                              "ConfirmInRangeBeforeAxesMove", "ConfirmInRangeAfterAxesMove",
                                              "ConfirmMatchBeforeAxesMove", "ConfirmMatchAfterAxesMove" };
    return t;
}

const std::vector<std::string>& JPActuatorConfig::Interlock::states() {
    static const std::vector<std::string> s { "SwitchedOff", "SwitchedJustOff", "SwitchedOn", "SwitchedJustOn",
                                              "SwitchedOffOrUnknown", "SwitchedJustOffOrUnknown", "SwitchedOnOrUnknown",
                                              "SwitchedJustOnOrUnknown" };
    return s;
}

const JPActuatorConfig::Profile* JPActuatorConfig::profileNamed(const std::string& profileName) const {
    for (const Profile& p : profiles)
        if (p.name == profileName) return &p;
    return nullptr;
}

const JPActuatorConfig::Profile* JPActuatorConfig::defaultProfile(bool on) const {
    for (const Profile& p : profiles)
        if (on ? p.defaultOn : p.defaultOff) return &p;
    return nullptr;
}

} // inline namespace jf
