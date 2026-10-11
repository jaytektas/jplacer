// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPNeoden4Feeder.h"

#include "JPLocationXml.h"

#include <cstdlib>

inline namespace jf {

JPNeoden4Feeder JPNeoden4Feeder::fromXml(const JPXmlNode& own) {
    JPNeoden4Feeder n;
    if (const std::string* v = own.get("actuator-name")) n.actuatorName = *v;
    if (const std::string* v = own.get("feed-count"); v && !v->empty()) n.feedCount = std::atoi(v->c_str());
    if (const JPXmlNode* p = own.child("part-pitch-in-tape"); p && p->get("value")) n.partPitchInTape = JPLocationXml::lengthFrom(*p);
    // OpenPnP keeps the rotation as an element.
    if (const JPXmlNode* r = own.child("part-rotation-in-tape"); r && !r->text.empty()) n.partRotationInTape = std::atoi(r->text.c_str());
    n.vision = JPTemplateVision::fromXml(own.child("vision"));
    return n;
}

JPNeoden4Feeder JPNeoden4Feeder::fromJson(const JJson& j) {
    JPNeoden4Feeder n;
    n.actuatorName = j["actuator"].str();
    n.feedCount = int(j["feedCount"].number(0));
    n.partPitchInTape = JPLength(j["partPitchMm"].number(4.0), JPLengthUnit::Millimeters);
    n.partRotationInTape = int(j["partRotation"].number(0));
    n.vision = JPTemplateVision::fromJson(j["vision"]);
    return n;
}

JJson JPNeoden4Feeder::toJson() const {
    JJson j = JJson::object();
    j["actuator"] = actuatorName;
    j["feedCount"] = feedCount;
    j["partPitchMm"] = partPitchInTape.convertToUnits(JPLengthUnit::Millimeters).value();
    j["partRotation"] = partRotationInTape;
    j["vision"] = vision.toJson();
    return j;
}

} // inline namespace jf
