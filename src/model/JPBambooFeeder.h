// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPVisionTape.h"

#include <string>

inline namespace jf {

// A Bamboo feeder (Pandaplacer's BambooFeederAutoVision): a tape fed by its feed actuator, calibrated by its
// sprocket holes. What a vision-calibrated tape has, and its feed actuator and value, the one actuated after a
// pick and its value, and whether the nozzle goes to the pick location before the feed.
class JPBambooFeeder : public JPVisionTape {
public:
    static constexpr const char* kClass = "org.openpnp.machine.pandaplacer.BambooFeederAutoVision";
    static constexpr const char* kKey = "bamboo";

    std::string feedActuatorName, postPickActuatorName;
    double      feedActuatorValue = 0, postPickActuatorValue = 0;
    bool        moveBeforeFeed = false;

    // OpenPnP's settings of it (its <feeder>'s own, what every feeder has taken out).
    static JPBambooFeeder fromXml(const JPXmlNode& own);
    // As jplacer's feeders.json keeps it: a vision tape's, and {"feedActuator", "feedActuatorValue",
    // "postPickActuator", "postPickActuatorValue", "moveBeforeFeed"}.
    static JPBambooFeeder fromJson(const JJson& j);
    JJson toJson() const;
};

} // inline namespace jf
