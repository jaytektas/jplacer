// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPLength.h"
#include "JPTemplateVision.h"

#include "openpnp/JPXmlNode.h"

#include <j/config/Json.h>

#include <string>

inline namespace jf {

// A NeoDen 4 feeder (OpenPnP's Neoden4Feeder): fed by its actuator, actuated with its pitch in tape. Its actuator,
// how many it has fed, the part pitch in its tape, the part's rotation in the tape (whole degrees), and the
// template vision that finds where its parts are now.
class JPNeoden4Feeder {
public:
    static constexpr const char* kClass = "org.openpnp.machine.neoden4.Neoden4Feeder";
    static constexpr const char* kKey = "neoden4";

    std::string      actuatorName;
    int              feedCount = 0;
    JPLength         partPitchInTape { 4, JPLengthUnit::Millimeters };
    int              partRotationInTape = 0;
    JPTemplateVision vision;

    // OpenPnP's settings of it (its <feeder>'s own, what every feeder has taken out).
    static JPNeoden4Feeder fromXml(const JPXmlNode& own);
    // As jplacer's feeders.json keeps it: {"actuator", "feedCount", "partPitchMm", "partRotation", "vision"}.
    static JPNeoden4Feeder fromJson(const JJson& j);
    JJson toJson() const;
};

} // inline namespace jf
