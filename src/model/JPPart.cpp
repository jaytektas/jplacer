// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPPart.h"

#include "JPXmlValues.h"

inline namespace jf {

using V = JPXmlValues;

JPPart JPPart::fromXml(const JPXmlElement& e) {
    JPPart p;
    p.id = e.attr("id");
    if (V::has(e, "name")) p.name = e.attr("name");
    p.height = JPLength(V::number(e, "height"), V::units(e, "height-units"));
    p.throughBoardDepth = JPLength(V::number(e, "through-board-depth"), V::units(e, "through-board-depth-units"));
    p.packageId = e.attr("package-id");
    p.speed = V::number(e, "speed", 1.0);
    p.pickRetryCount = V::integer(e, "pick-retry-count");
    p.bottomVisionId = e.attr("bottom-vision-id");
    p.fiducialVisionId = e.attr("fiducial-vision-id");
    return p;
}

JPXmlNode JPPart::toXml() const {
    JPXmlNode n("part");
    if (!bottomVisionId.empty()) n.attr("bottom-vision-id", bottomVisionId);
    if (!fiducialVisionId.empty()) n.attr("fiducial-vision-id", fiducialVisionId);
    n.attr("id", id);
    if (name) n.attr("name", *name);
    n.attr("height-units", V::units(height.units()))
        .attr("height", V::number(height.value()))
        .attr("through-board-depth-units", V::units(throughBoardDepth.units()))
        .attr("through-board-depth", V::number(throughBoardDepth.value()));
    if (!packageId.empty()) n.attr("package-id", packageId);
    n.attr("speed", V::number(speed)).attr("pick-retry-count", std::to_string(pickRetryCount));
    return n;
}

} // inline namespace jf
