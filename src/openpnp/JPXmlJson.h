// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPXmlElement.h"
#include "JPXmlNode.h"

#include <j/config/Json.h>

inline namespace jf {

// An OpenPnP XML element as JSON and back, losing nothing: {"tag", "attrs"
// (in the element's order, as pairs), "text", "children"}. For what a JSON
// file of jplacer's carries in OpenPnP's shape (a part or package of the
// library's, a profile), read and written by the same code as the XML.
class JPXmlJson {
public:
    static JJson from(const JPXmlNode& n);
    static JPXmlElement element(const JJson& j);
};

} // inline namespace jf
