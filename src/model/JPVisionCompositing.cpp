// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPVisionCompositing.h"

#include "JPLocationXml.h"
#include "JPXmlValues.h"

inline namespace jf {

using V = JPXmlValues;

const char* JPVisionCompositing::methodName(Method m) {
    switch (m) {
        case Method::None:          return "None";
        case Method::Restricted:    return "Restricted";
        case Method::Body:          return "Body";
        case Method::Automatic:     return "Automatic";
        case Method::SingleCorners: return "SingleCorners";
    }
    return "Restricted";
}

JPVisionCompositing::Method JPVisionCompositing::methodFrom(const std::string& s) {
    for (const Method m : { Method::None, Method::Restricted, Method::Body, Method::Automatic, Method::SingleCorners })
        if (s == methodName(m)) return m;
    return Method::Restricted;
}

JPVisionCompositing JPVisionCompositing::fromXml(const JPXmlElement& e) {
    JPVisionCompositing v;
    v.compositingMethod = methodFrom(e.attr("compositing-method"));
    v.minLeverageFactor = V::number(e, "min-leverage-factor", 0.2);
    v.extraShots = V::integer(e, "extra-shots");
    v.allowInside = V::boolean(e, "allow-inside", true);
    if (const JPXmlElement* t = e.child("max-pick-tolerance")) v.maxPickTolerance = JPLocationXml::lengthFrom(*t);
    return v;
}

JPXmlNode JPVisionCompositing::toXml() const {
    JPXmlNode n("vision-compositing");
    n.attr("compositing-method", methodName(compositingMethod))
        .attr("min-leverage-factor", V::number(minLeverageFactor))
        .attr("extra-shots", std::to_string(extraShots))
        .attr("allow-inside", V::boolean(allowInside));
    n.add(JPLocationXml::lengthTo("max-pick-tolerance", maxPickTolerance));
    return n;
}

} // inline namespace jf
