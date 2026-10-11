// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPRapidFeeder.h"

#include "JPLocationJson.h"
#include "JPLocationXml.h"

#include <cstdlib>

inline namespace jf {

JPRapidFeeder JPRapidFeeder::fromXml(const JPXmlNode& own) {
    JPRapidFeeder r;
    if (const std::string* v = own.get("address")) r.address = *v;
    if (const std::string* v = own.get("pitch"); v && !v->empty()) r.pitch = std::atoi(v->c_str());
    if (const JPXmlNode* l = own.child("scan-start-location")) r.scanStart = JPLocationXml::from(*l);
    if (const JPXmlNode* l = own.child("scan-end-location")) r.scanEnd = JPLocationXml::from(*l);
    if (const JPXmlNode* l = own.child("scan-increment"); l && l->get("value"))
        r.scanIncrement = JPLocationXml::lengthFrom(*l);
    return r;
}

JPRapidFeeder JPRapidFeeder::fromJson(const JJson& j) {
    JPRapidFeeder r;
    r.address = j["address"].str();
    r.pitch = int(j["pitch"].number(4));
    r.scanStart = JPLocationJson::from(j["scanStart"]);
    r.scanEnd = JPLocationJson::from(j["scanEnd"]);
    r.scanIncrement = JPLength(j["scanIncrementMm"].number(4.0), JPLengthUnit::Millimeters);
    return r;
}

JJson JPRapidFeeder::toJson() const {
    JJson j = JJson::object();
    j["address"] = address;
    j["pitch"] = pitch;
    j["scanStart"] = JPLocationJson::to(scanStart);
    j["scanEnd"] = JPLocationJson::to(scanEnd);
    j["scanIncrementMm"] = scanIncrement.convertToUnits(JPLengthUnit::Millimeters).value();
    return j;
}

} // inline namespace jf
