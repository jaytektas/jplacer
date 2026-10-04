// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPBoardPad.h"

#include "JPLocationXml.h"
#include "JPSides.h"
#include "JPXmlValues.h"

inline namespace jf {

using V = JPXmlValues;

JPBoardPad JPBoardPad::fromXml(const JPXmlElement& e) {
    JPBoardPad b;
    b.type = e.attr("type") == "Ignore" ? Type::Ignore : Type::Paste;
    b.side = JPSides::fromName(e.attr("side"));
    if (V::has(e, "name")) b.name = e.attr("name");
    if (const JPXmlElement* l = e.child("location")) b.location = JPLocationXml::from(*l);
    if (const JPXmlElement* p = e.child("pad")) {
        b.pad.name = p->attr("name");
        b.pad.x = V::number(*p, "x");
        b.pad.y = V::number(*p, "y");
        b.pad.width = V::number(*p, "width");
        b.pad.height = V::number(*p, "height");
        b.pad.rotation = V::number(*p, "rotation");
        b.pad.mark = V::boolean(*p, "mark");
        b.pad.roundness = V::number(*p, "roundness");
    }
    return b;
}

JPXmlNode JPBoardPad::toXml() const {
    JPXmlNode n("board-pad");
    n.attr("type", type == Type::Ignore ? "Ignore" : "Paste").attr("side", JPSides::name(side));
    if (name) n.attr("name", *name);
    n.add(JPLocationXml::to("location", location));
    n.add(JPXmlNode("pad"))
        .attr("name", pad.name)
        .attr("x", V::number(pad.x))
        .attr("y", V::number(pad.y))
        .attr("width", V::number(pad.width))
        .attr("height", V::number(pad.height))
        .attr("rotation", V::number(pad.rotation))
        .attr("mark", V::boolean(pad.mark))
        .attr("roundness", V::number(pad.roundness));
    return n;
}

} // inline namespace jf
