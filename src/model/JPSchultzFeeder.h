// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "openpnp/JPXmlNode.h"

#include <j/config/Json.h>

#include <string>

inline namespace jf {

// A Schultz feeder (OpenPnP's SchultzFeeder): fed by its pre pick actuator, each of its actuators given its
// feeder number. The actuators that read its ID, feed count, pitch and status and that clear the count and toggle
// the pitch; and the fiducial part its location is found by.
class JPSchultzFeeder {
public:
    static constexpr const char* kClass = "org.openpnp.machine.reference.feeder.SchultzFeeder";
    static constexpr const char* kKey = "schultz";

    std::string actuatorName;   // pre pick
    double      actuatorValue = 0;   // its feeder number
    std::string postPickActuatorName, feedCountActuatorName, clearCountActuatorName, pitchActuatorName,
                togglePitchActuatorName, statusActuatorName, idActuatorName;
    std::string fiducialPart;

    // OpenPnP's settings of it (its <feeder>'s own, what every feeder has taken out).
    static JPSchultzFeeder fromXml(const JPXmlNode& own);
    // As jplacer's feeders.json keeps it: {"feederNumber", "prePickActuator", "postPickActuator",
    // "feedCountActuator", "clearCountActuator", "pitchActuator", "togglePitchActuator", "statusActuator",
    // "idActuator", "fiducialPart"}.
    static JPSchultzFeeder fromJson(const JJson& j);
    JJson toJson() const;
};

} // inline namespace jf
