// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "openpnp/JPXmlNode.h"

#include <j/config/Json.h>

inline namespace jf {

// A tube feeder (OpenPnP's ReferenceTubeFeeder): parts pushed to its end, each picked where its feeder is. It
// has no settings of its own; feeding it moves nothing.
class JPTubeFeeder {
public:
    static constexpr const char* kClass = "org.openpnp.machine.reference.feeder.ReferenceTubeFeeder";
    static constexpr const char* kKey = "tube";

    static JPTubeFeeder fromXml(const JPXmlNode&) { return {}; }
    static JPTubeFeeder fromJson(const JJson&) { return {}; }
    JJson toJson() const { return JJson::object(); }
};

} // inline namespace jf
