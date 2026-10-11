// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPLoosePartFeeder.h"

#include "openpnp/JPXmlJson.h"

inline namespace jf {

JPLoosePartFeeder JPLoosePartFeeder::fromXml(const JPXmlNode& own) {
    JPLoosePartFeeder l;
    if (const JPXmlNode* p = own.child("pipeline")) l.pipeline = *p;
    return l;
}

JPLoosePartFeeder JPLoosePartFeeder::fromJson(const JJson& j) {
    JPLoosePartFeeder l;
    if (j["pipeline"].isObject()) l.pipeline = JPXmlJson::node("pipeline", j["pipeline"]);
    return l;
}

JJson JPLoosePartFeeder::toJson() const {
    JJson j = JJson::object();
    if (pipeline) j["pipeline"] = JPXmlJson::from(*pipeline);
    return j;
}

} // inline namespace jf
