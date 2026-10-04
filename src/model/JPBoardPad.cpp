// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPBoardPad.h"

#include "JPLengthUnits.h"
#include "JPLocationXml.h"
#include "JPSides.h"
#include "JPXmlValues.h"

inline namespace jf {

using V = JPXmlValues;

namespace {
// The class names OpenPnP's files give a pad's shape.
constexpr const char* kRoundRectangle = "org.openpnp.model.Pad$RoundRectangle";
constexpr const char* kCircle         = "org.openpnp.model.Pad$Circle";
constexpr const char* kEllipse        = "org.openpnp.model.Pad$Ellipse";
}

JPBoardPad JPBoardPad::fromXml(const JPXmlElement& e) {
    JPBoardPad b;
    b.type = e.attr("type") == "Ignore" ? Type::Ignore : Type::Paste;
    b.side = JPSides::fromName(e.attr("side"));
    if (V::has(e, "name")) b.name = e.attr("name");
    if (const JPXmlElement* l = e.child("location")) b.location = JPLocationXml::from(*l);
    if (const JPXmlElement* p = e.child("pad")) {
        const std::string cls = p->attr("class");
        b.pad.kind = cls == kCircle ? Shape::Kind::Circle : cls == kEllipse ? Shape::Kind::Ellipse : Shape::Kind::RoundRectangle;
        JPLengthUnits::fromName(p->attr("units"), b.pad.units);
        b.pad.width = V::number(*p, "width");
        b.pad.height = V::number(*p, "height");
        b.pad.roundness = V::number(*p, "roundness");
        b.pad.radius = V::number(*p, "radius");
    }
    return b;
}

JPXmlNode JPBoardPad::toXml() const {
    JPXmlNode n("board-pad");
    n.attr("type", type == Type::Ignore ? "Ignore" : "Paste").attr("side", JPSides::name(side));
    if (name) n.attr("name", *name);
    n.add(JPLocationXml::to("location", location));
    JPXmlNode& p = n.add(JPXmlNode("pad"));
    p.attr("class", pad.kind == Shape::Kind::Circle    ? kCircle
                    : pad.kind == Shape::Kind::Ellipse ? kEllipse
                                                       : kRoundRectangle)
        .attr("units", JPLengthUnits::name(pad.units));
    if (pad.kind == Shape::Kind::Circle) {
        p.attr("radius", V::number(pad.radius));
    } else {
        p.attr("width", V::number(pad.width)).attr("height", V::number(pad.height));
        if (pad.kind == Shape::Kind::RoundRectangle) p.attr("roundness", V::number(pad.roundness));
    }
    return n;
}

} // inline namespace jf
