// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPPackage.h"

#include <algorithm>

inline namespace jf {

namespace {

std::vector<std::string> strings(const JJson& a) {
    std::vector<std::string> out;
    for (const JJson& s : a.arr()) out.push_back(s.str());
    return out;
}

JJson array(const std::vector<std::string>& v) {
    JJson a = JJson::array();
    for (const std::string& s : v) a.push(s);
    return a;
}

} // namespace

bool JPPackage::hasName(const std::string& n) const {
    return std::find(names.begin(), names.end(), n) != names.end();
}

JPPackage JPPackage::fromJson(const JJson& j) {
    JPPackage p;
    p.id          = j["id"].str();
    p.name        = j["name"].str();
    p.footprintId = j["footprintId"].str();
    p.length      = j["length"].number();
    p.width       = j["width"].number();
    p.height      = j["height"].number();
    p.tipIds      = strings(j["tipIds"]);
    p.speed       = j["speed"].number();
    p.pickRetries = int(j["pickRetries"].number(-1.0));
    p.names       = strings(j["names"]);
    p.turnDeg     = j["turn"].number();
    p.revision    = int(j["revision"].number(1.0));
    if (j.contains("origin")) p.origin = JPOrigin::fromJson(j["origin"]);
    return p;
}

JJson JPPackage::toJson() const {
    JJson j = JJson::object();
    j["id"]          = id;
    j["name"]        = name;
    j["footprintId"] = footprintId;
    j["length"]      = length;
    j["width"]       = width;
    j["height"]      = height;
    j["tipIds"]      = array(tipIds);
    j["speed"]       = speed;
    j["pickRetries"] = pickRetries;
    j["names"]       = array(names);
    j["turn"]        = turnDeg;
    j["revision"]    = revision;
    if (origin.fromLibrary()) j["origin"] = origin.toJson();
    return j;
}

} // inline namespace jf
