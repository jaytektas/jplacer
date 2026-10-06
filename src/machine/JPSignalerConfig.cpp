// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPSignalerConfig.h"

#include <algorithm>

inline namespace jf {

const std::vector<std::string>& JPSignalerConfig::classNames() {
    static const std::vector<std::string> n { "SoundSignaler", "ActuatorSignaler", "Neoden4Signaler" };
    return n;
}

const std::vector<std::string>& JPSignalerConfig::jobStateKeys() {
    static const std::vector<std::string> k { "STOPPED", "RUNNING", "ERROR", "FINISHED" };
    return k;
}

JJson JPSignalerConfig::toJson() const {
    JJson j = JJson::object();
    j["class"] = className();
    j["id"] = id;
    j["name"] = name;
    if (kind != Kind::Actuator) {
        j["errorSound"] = errorSound;
        j["finishedSound"] = finishedSound;
    } else {
        j["actuatorId"] = actuatorId;
        if (jobState) j["jobState"] = jobStateKeys()[size_t(*jobState)];
    }
    return j;
}

JPSignalerConfig JPSignalerConfig::fromJson(const JJson& j) {
    JPSignalerConfig s;
    const auto& names = classNames();
    if (const auto it = std::find(names.begin(), names.end(), j["class"].str()); it != names.end())
        s.kind = Kind(it - names.begin());
    s.id = j["id"].str();
    s.name = j["name"].str();
    s.errorSound = j["errorSound"].boolean();
    s.finishedSound = j["finishedSound"].boolean();
    s.actuatorId = j["actuatorId"].str();
    const auto& states = jobStateKeys();
    if (const auto it = std::find(states.begin(), states.end(), j["jobState"].str()); it != states.end())
        s.jobState = JobState(it - states.begin());
    return s;
}

} // inline namespace jf
