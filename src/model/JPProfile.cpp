// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPProfile.h"

#include "JPXmlValues.h"

#include <cstdlib>

inline namespace jf {

using V = JPXmlValues;

JPProfile JPProfile::rectangle(double w, double h, JPLengthUnit units) {
    JPProfile p;
    p.units = units;
    p.windingRule = 1;   // Path2D.WIND_NON_ZERO
    p.segmentTypes = { MoveTo, LineTo, LineTo, LineTo, Close };
    p.segmentPoints = { 0, 0, w, 0, w, h, 0, h };
    return p;
}

JPProfile JPProfile::fromXml(const JPXmlElement& e) {
    JPProfile p;
    p.units = V::units(e, "units");
    if (V::has(e, "winding-rule")) p.windingRule = V::integer(e, "winding-rule");
    if (const JPXmlElement* t = e.child("segment-types"))
        for (const JPXmlElement& c : t->children) p.segmentTypes.push_back(std::atoi(V::text(c).c_str()));
    if (const JPXmlElement* t = e.child("segment-points"))
        for (const JPXmlElement& c : t->children) p.segmentPoints.push_back(std::strtod(V::text(c).c_str(), nullptr));
    return p;
}

JPXmlNode JPProfile::toXml(const char* name) const {
    JPXmlNode n(name);
    n.attr("units", V::units(units));
    if (windingRule) n.attr("winding-rule", std::to_string(*windingRule));
    JPXmlNode& types = n.add(JPXmlNode("segment-types"));
    types.attr("class", "java.util.ArrayList");
    for (const int t : segmentTypes) types.add(JPXmlNode("int")).text = std::to_string(t);
    JPXmlNode& points = n.add(JPXmlNode("segment-points"));
    points.attr("class", "java.util.ArrayList");
    for (const double d : segmentPoints) points.add(JPXmlNode("double")).text = V::number(d);
    return n;
}

} // inline namespace jf
