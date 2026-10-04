// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPLength.h"
#include "JPLocation.h"

#include "openpnp/JPXmlElement.h"
#include "openpnp/JPXmlNode.h"

#include <string>

inline namespace jf {

// A Location or a Length as an element of OpenPnP's files:
// <location units="Millimeters" x="…" y="…" z="…" rotation="…"/> and
// <max-pick-tolerance value="…" units="…"/>.
struct JPLocationXml {
    static JPLocation from(const JPXmlElement& e);
    static JPXmlNode  to(const std::string& name, const JPLocation& l);
    static JPLength   lengthFrom(const JPXmlElement& e);
    static JPXmlNode  lengthTo(const std::string& name, const JPLength& l);
};

} // inline namespace jf
