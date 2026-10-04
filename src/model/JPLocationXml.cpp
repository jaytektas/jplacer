// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPLocationXml.h"

#include "JPXmlValues.h"

inline namespace jf {

using V = JPXmlValues;

JPLocation JPLocationXml::from(const JPXmlElement& e) {
    return JPLocation(V::units(e, "units"), V::number(e, "x"), V::number(e, "y"), V::number(e, "z"), V::number(e, "rotation"));
}

JPXmlNode JPLocationXml::to(const std::string& name, const JPLocation& l) {
    JPXmlNode n(name);
    n.attr("units", V::units(l.units()))
        .attr("x", V::number(l.x()))
        .attr("y", V::number(l.y()))
        .attr("z", V::number(l.z()))
        .attr("rotation", V::number(l.rotation()));
    return n;
}

JPLength JPLocationXml::lengthFrom(const JPXmlElement& e) {
    return JPLength(V::number(e, "value"), V::units(e, "units"));
}

JPXmlNode JPLocationXml::lengthTo(const std::string& name, const JPLength& l) {
    JPXmlNode n(name);
    n.attr("value", V::number(l.value())).attr("units", V::units(l.units()));
    return n;
}

} // inline namespace jf
