// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPLength.h"
#include "JPLocation.h"
#include "JPTemplateVision.h"

#include "openpnp/JPXmlNode.h"

#include <j/config/Json.h>

#include <string>

inline namespace jf {

// What a feeder moved on by a pin has (OpenPnP's drag and lever feeders, JPDragFeeder and JPLeverFeeder): where
// its actuator's pin starts and ends its stroke, the part pitch, how fast the stroke is (a share of full speed),
// the pin's actuator and the one that peels the cover tape off, and the template vision that finds where the
// parts are now.
class JPPinFeeder {
public:
    JPLocation       feedStart { JPLengthUnit::Millimeters }, feedEnd { JPLengthUnit::Millimeters };
    JPLength         partPitch { 4, JPLengthUnit::Millimeters };
    double           feedSpeed = 1.0;
    std::string      actuatorName, peelOffActuatorName;
    JPTemplateVision vision;

protected:
    // OpenPnP's settings of it, and jplacer's: {"feedStart", "feedEnd", "partPitchMm", "feedSpeed", "actuator",
    // "peelOffActuator", "vision"}.
    void readXml(const JPXmlNode& own);
    void readJson(const JJson& j);
    JJson json() const;
};

} // inline namespace jf
