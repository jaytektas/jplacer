// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPPipelineValue.h"

#include "openpnp/JPXmlNode.h"

#include <map>
#include <string>

inline namespace jf {

// The values a vision setting assigns to its pipeline's parameters
// (OpenPnP's pipeline-parameter-assignments: <entry><string>name</string>
// <object class="java.lang.Integer">204</object></entry>…), by parameter:
// integers, numbers, flags, texts, lengths and areas (in millimetres).
class JPPipelineAssignments {
public:
    using Map = std::map<std::string, JPPipelineValue>;

    static Map       fromXml(const JPXmlNode* node);
    static JPXmlNode toXml(const Map& assignments);
};

} // inline namespace jf
