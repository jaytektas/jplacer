// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPPhotonFeeder.h"

#include "JPLocationJson.h"
#include "JPLocationXml.h"

#include <cstdlib>

inline namespace jf {

JPPhotonFeeder JPPhotonFeeder::fromXml(const JPXmlNode& own) {
    JPPhotonFeeder p;
    if (const std::string* v = own.get("hardware-id")) p.hardwareId = *v;
    if (const std::string* v = own.get("part-pitch"); v && !v->empty()) p.partPitch = std::atoi(v->c_str());
    if (const JPXmlNode* o = own.child("offset")) p.offset = JPLocationXml::from(*o);
    if (const std::string* v = own.get("move-while-feeding")) p.moveWhileFeeding = *v == "true";
    return p;
}

JPPhotonFeeder JPPhotonFeeder::fromJson(const JJson& j) {
    JPPhotonFeeder p;
    p.hardwareId = j["hardwareId"].str();
    p.partPitch = int(j["partPitch"].number(4));
    if (j["offset"].isObject()) p.offset = JPLocationJson::from(j["offset"]);
    p.moveWhileFeeding = j["moveWhileFeeding"].boolean(true);
    return p;
}

JJson JPPhotonFeeder::toJson() const {
    JJson j = JJson::object();
    j["hardwareId"] = hardwareId;
    j["partPitch"] = partPitch;
    if (offset) j["offset"] = JPLocationJson::to(*offset);
    j["moveWhileFeeding"] = moveWhileFeeding;
    return j;
}

} // inline namespace jf
