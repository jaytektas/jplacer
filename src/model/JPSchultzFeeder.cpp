// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPSchultzFeeder.h"

#include <cstdlib>

inline namespace jf {

namespace {

// Each actuator's OpenPnP name and jplacer's.
struct Actuator {
    std::string JPSchultzFeeder::* field;
    const char*                    openPnp;
    const char*                    key;
};
const Actuator kActuators[] = {
    { &JPSchultzFeeder::actuatorName, "actuator-name", "prePickActuator" },
    { &JPSchultzFeeder::postPickActuatorName, "post-pick-actuator-name", "postPickActuator" },
    { &JPSchultzFeeder::feedCountActuatorName, "feed-count-actuator-name", "feedCountActuator" },
    { &JPSchultzFeeder::clearCountActuatorName, "clear-count-actuator-name", "clearCountActuator" },
    { &JPSchultzFeeder::pitchActuatorName, "pitch-actuator-name", "pitchActuator" },
    { &JPSchultzFeeder::togglePitchActuatorName, "toggle-pitch-actuator-name", "togglePitchActuator" },
    { &JPSchultzFeeder::statusActuatorName, "status-actuator-name", "statusActuator" },
    { &JPSchultzFeeder::idActuatorName, "id-actuator-name", "idActuator" },
};

} // namespace

JPSchultzFeeder JPSchultzFeeder::fromXml(const JPXmlNode& own) {
    JPSchultzFeeder s;
    for (const Actuator& a : kActuators)
        if (const std::string* v = own.get(a.openPnp)) s.*a.field = *v;
    if (const std::string* v = own.get("actuator-value"); v && !v->empty()) s.actuatorValue = std::strtod(v->c_str(), nullptr);
    if (const std::string* v = own.get("fiducial-part")) s.fiducialPart = *v;
    return s;
}

JPSchultzFeeder JPSchultzFeeder::fromJson(const JJson& j) {
    JPSchultzFeeder s;
    for (const Actuator& a : kActuators) s.*a.field = j[a.key].str();
    s.actuatorValue = j["feederNumber"].number(0.0);
    s.fiducialPart = j["fiducialPart"].str();
    return s;
}

JJson JPSchultzFeeder::toJson() const {
    JJson j = JJson::object();
    j["feederNumber"] = actuatorValue;
    for (const Actuator& a : kActuators) j[a.key] = this->*a.field;
    j["fiducialPart"] = fiducialPart;
    return j;
}

} // inline namespace jf
