// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPBoard.h"

#include "JPLocationXml.h"

inline namespace jf {

namespace {
constexpr const char* kVersion = "1.1";
}

std::shared_ptr<JPPlacementsHolder> JPBoard::instance() const {
    auto b = std::make_shared<JPBoard>(*this);
    b->m_definition = definition();
    return b;
}

JPBoard JPBoard::fromXml(const JPXmlElement& root) {
    JPBoard b;
    if (root.attributes.count("name")) b.name = root.attr("name");
    if (const JPXmlElement* d = root.child("dimensions")) b.dimensions = JPLocationXml::from(*d);
    if (const JPXmlElement* ps = root.child("placements"))
        for (const JPXmlElement& p : ps->children)
            if (p.name == "placement") b.placements.push_back(JPPlacement::fromXml(p));
    if (const JPXmlElement* p = root.child("profile")) b.profile = JPProfile::fromXml(*p);
    if (const JPXmlElement* pads = root.child("solder-paste-pads"))
        for (const JPXmlElement& p : pads->children) b.solderPastePads.push_back(JPBoardPad::fromXml(p));
    return b;
}

JPXmlNode JPBoard::toXml() const {
    JPXmlNode n("openpnp-board");
    n.attr("version", kVersion);
    if (name) n.attr("name", *name);
    n.add(JPLocationXml::to("dimensions", dimensions));
    JPXmlNode& ps = n.add(JPXmlNode("placements"));
    for (const JPPlacement& p : placements) ps.add(p.toXml());
    if (profile) n.add(profile->toXml());
    n.add(JPXmlNode("fiducials"));
    JPXmlNode& pads = n.add(JPXmlNode("solder-paste-pads"));
    for (const JPBoardPad& p : solderPastePads) pads.add(p.toXml());
    return n;
}

} // inline namespace jf
