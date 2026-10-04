// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPVisionSettings.h"

#include "JPXmlValues.h"

inline namespace jf {

JPVisionSettings JPVisionSettings::fromXml(const JPXmlElement& e) {
    JPVisionSettings v;
    v.id = e.attr("id");
    v.name = e.attr("name");
    v.enabled = JPXmlValues::boolean(e, "enabled", true);
    const std::string cls = e.attr("class");
    if (cls == "org.openpnp.model.BottomVisionSettings") v.kind = Kind::Bottom;
    else if (cls == "org.openpnp.model.FiducialVisionSettings") v.kind = Kind::Fiducial;
    return v;
}

} // inline namespace jf
