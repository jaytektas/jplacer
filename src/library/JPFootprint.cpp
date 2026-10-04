// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPFootprint.h"

inline namespace jf {

const JPPad* JPFootprint::pad(const std::string& padName) const {
    for (const JPPad& p : pads)
        if (p.name == padName) return &p;
    return nullptr;
}

JPFootprint JPFootprint::fromJson(const JJson& j) {
    JPFootprint f;
    f.id         = j["id"].str();
    f.name       = j["name"].str();
    for (const JJson& p : j["pads"].arr()) f.pads.push_back(JPPad::fromJson(p));
    f.bodyWidth  = j["bodyWidth"].number();
    f.bodyLength = j["bodyLength"].number();
    f.pin1       = j["pin1"].str();
    f.revision   = int(j["revision"].number(1.0));
    if (j.contains("origin")) f.origin = JPOrigin::fromJson(j["origin"]);
    return f;
}

JJson JPFootprint::toJson() const {
    JJson j = JJson::object();
    j["id"]   = id;
    j["name"] = name;
    JJson pa = JJson::array();
    for (const JPPad& p : pads) pa.push(p.toJson());
    j["pads"]       = std::move(pa);
    j["bodyWidth"]  = bodyWidth;
    j["bodyLength"] = bodyLength;
    j["pin1"]       = pin1;
    j["revision"]   = revision;
    if (origin.fromLibrary()) j["origin"] = origin.toJson();
    return j;
}

} // inline namespace jf
