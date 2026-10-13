// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPPackage.h"

#include "JPXmlValues.h"

inline namespace jf {

using V = JPXmlValues;

namespace {
constexpr const char* kVersion = "1.1";
}

JPPackage JPPackage::fromXml(const JPXmlElement& e) {
    JPPackage p;
    p.id = e.attr("id");   // OpenPnP's file's: its name (the caller makes it the label)
    if (V::has(e, "description")) p.description = e.attr("description");
    if (V::has(e, "height")) p.height = JPLength(V::number(e, "height"), V::units(e, "height-units"));
    p.padsSource = e.attr("pads-source");
    p.bodySource = e.attr("body-source");
    p.heightSource = e.attr("height-source");
    p.pickVacuumLevel = V::number(e, "pick-vacuum-level");
    p.placeBlowOffLevel = V::number(e, "place-blow-off-level");
    p.bottomVisionId = e.attr("bottom-vision-id");
    p.fiducialVisionId = e.attr("fiducial-vision-id");
    if (const JPXmlElement* f = e.child("footprint")) p.footprint = JPFootprint::fromXml(*f);
    if (const JPXmlElement* v = e.child("vision-compositing")) p.visionCompositing = JPVisionCompositing::fromXml(*v);
    if (const JPXmlElement* ids = e.child("compatible-nozzle-tip-ids"))
        for (const JPXmlElement& s : ids->children)
            if (s.name == "string") p.compatibleNozzleTipIds.push_back(V::text(s));
    return p;
}

JPXmlNode JPPackage::toXml() const {
    JPXmlNode n("package");
    n.attr("version", kVersion);
    if (!bottomVisionId.empty()) n.attr("bottom-vision-id", bottomVisionId);
    if (!fiducialVisionId.empty()) n.attr("fiducial-vision-id", fiducialVisionId);
    n.attr("id", id);
    if (description) n.attr("description", *description);
    // jplacer's own (OpenPnP's packages have none): written only when known, so one without stays OpenPnP's.
    if (height.value() > 0) n.attr("height-units", V::units(height.units())).attr("height", V::number(height.value()));
    if (!padsSource.empty()) n.attr("pads-source", padsSource);
    if (!bodySource.empty()) n.attr("body-source", bodySource);
    if (!heightSource.empty()) n.attr("height-source", heightSource);
    n.attr("pick-vacuum-level", V::number(pickVacuumLevel)).attr("place-blow-off-level", V::number(placeBlowOffLevel));
    n.add(footprint.toXml());
    if (visionCompositing) n.add(visionCompositing->toXml());
    JPXmlNode& ids = n.add(JPXmlNode("compatible-nozzle-tip-ids"));
    ids.attr("class", "java.util.ArrayList");
    for (const std::string& s : compatibleNozzleTipIds) ids.add(JPXmlNode("string")).text = s;
    return n;
}

} // inline namespace jf
