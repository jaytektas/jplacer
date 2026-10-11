// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPDragFeeder.h"

#include "JPLocationXml.h"

inline namespace jf {

JPDragFeeder JPDragFeeder::fromXml(const JPXmlNode& own) {
    JPDragFeeder d;
    d.readXml(own);
    if (const JPXmlNode* b = own.child("backoff-distance"); b && b->get("value")) d.backoffDistance = JPLocationXml::lengthFrom(*b);
    return d;
}

JPDragFeeder JPDragFeeder::fromJson(const JJson& j) {
    JPDragFeeder d;
    d.readJson(j);
    d.backoffDistance = JPLength(j["backoffDistanceMm"].number(0.0), JPLengthUnit::Millimeters);
    return d;
}

JJson JPDragFeeder::toJson() const {
    JJson j = json();
    j["backoffDistanceMm"] = backoffDistance.convertToUnits(JPLengthUnit::Millimeters).value();
    return j;
}

} // inline namespace jf
