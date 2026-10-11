// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "openpnp/JPXmlNode.h"

#include <j/config/Json.h>

#include <string>

inline namespace jf {

// An auto feeder (OpenPnP's ReferenceAutoFeeder): one that feeds itself when its feed actuator is actuated, each
// part picked where its feeder is. Its feed actuator and the value it is actuated with; the one actuated after a
// pick, and its value; whether the nozzle goes to the pick location before the feed; and whether a part can be
// put back into it (recycled).
class JPAutoFeeder {
public:
    static constexpr const char* kClass = "org.openpnp.machine.reference.feeder.ReferenceAutoFeeder";
    static constexpr const char* kKey = "auto";

    std::string actuatorName;
    double      actuatorValue = 0;
    std::string postPickActuatorName;
    double      postPickActuatorValue = 0;
    bool        moveBeforeFeed = false;
    bool        recycleSupport = false;

    // OpenPnP's settings of it (its <feeder>'s own, what every feeder has taken out).
    static JPAutoFeeder fromXml(const JPXmlNode& own);
    // As jplacer's feeders.json keeps it: {"actuator", "actuatorValue", "postPickActuator", "postPickActuatorValue",
    // "moveBeforeFeed", "recycleSupport"}.
    static JPAutoFeeder fromJson(const JJson& j);
    JJson toJson() const;
};

} // inline namespace jf
