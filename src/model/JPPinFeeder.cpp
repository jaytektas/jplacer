// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPPinFeeder.h"

#include "JPLocationJson.h"
#include "JPLocationXml.h"

#include <cstdlib>

inline namespace jf {

void JPPinFeeder::readXml(const JPXmlNode& own) {
    if (const JPXmlNode* l = own.child("feed-start-location")) feedStart = JPLocationXml::from(*l);
    if (const JPXmlNode* l = own.child("feed-end-location")) feedEnd = JPLocationXml::from(*l);
    if (const JPXmlNode* p = own.child("part-pitch"); p && p->get("value")) partPitch = JPLocationXml::lengthFrom(*p);
    // OpenPnP keeps the speed as an element.
    if (const JPXmlNode* s = own.child("feed-speed"); s && !s->text.empty()) feedSpeed = std::strtod(s->text.c_str(), nullptr);
    if (const std::string* v = own.get("actuator-name")) actuatorName = *v;
    if (const std::string* v = own.get("peel-off-actuator-name")) peelOffActuatorName = *v;
    vision = JPTemplateVision::fromXml(own.child("vision"));
}

void JPPinFeeder::readJson(const JJson& j) {
    feedStart = JPLocationJson::from(j["feedStart"]);
    feedEnd = JPLocationJson::from(j["feedEnd"]);
    partPitch = JPLength(j["partPitchMm"].number(4.0), JPLengthUnit::Millimeters);
    feedSpeed = j["feedSpeed"].number(1.0);
    actuatorName = j["actuator"].str();
    peelOffActuatorName = j["peelOffActuator"].str();
    vision = JPTemplateVision::fromJson(j["vision"]);
}

JJson JPPinFeeder::json() const {
    JJson j = JJson::object();
    j["feedStart"] = JPLocationJson::to(feedStart);
    j["feedEnd"] = JPLocationJson::to(feedEnd);
    j["partPitchMm"] = partPitch.convertToUnits(JPLengthUnit::Millimeters).value();
    j["feedSpeed"] = feedSpeed;
    j["actuator"] = actuatorName;
    j["peelOffActuator"] = peelOffActuatorName;
    j["vision"] = vision.toJson();
    return j;
}

} // inline namespace jf
