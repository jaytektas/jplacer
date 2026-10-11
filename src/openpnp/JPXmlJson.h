// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPXmlElement.h"
#include "JPXmlNode.h"

#include <j/config/Json.h>

#include <string>

inline namespace jf {

// What an OpenPnP-shaped element (a feeder, vision settings, a pipeline, a
// part or package of the library's, a profile) is in jplacer's JSON files,
// read and worked on by the same code as OpenPnP's XML. An element is an
// object, its name the key it is under (the caller's to give at the top):
//
//   attributes     "x": 1.5, "enabled": true, "id": "FDR1"   (in order)
//   a child        "location": { "units": "Millimeters", "x": 0.0 }
//   children of one name, more than one
//                  "cv-stage": [ { … }, { … } ]   (a text-only one: its text)
//   text           "#text": "0.0"   (a text-only child: { "#text": … }; only spaces and new lines
//                  between children: none)
//
// An attribute is a boolean when "true" or "false", a number when it reads
// back as written (an integer, or Java's double: "1.5", "1.0E-4"); written
// back, a whole number is an integer ("0.0" comes back as "0"). Children of
// different names keep the order each name first comes in. An element whose
// attribute and child share a name keeps its children in "#children", each
// naming itself in "#name".
class JPXmlJson {
public:
    static JJson from(const JPXmlNode& n);
    static JPXmlElement element(const std::string& name, const JJson& j);
    static JPXmlNode node(const std::string& name, const JJson& j) { return JPXmlNode::from(element(name, j)); }
};

} // inline namespace jf
