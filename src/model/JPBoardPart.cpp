// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPBoardPart.h"

#include "JPLibraryJson.h"

#include "openpnp/JPXmlJson.h"

inline namespace jf {

std::string JPBoardPart::partId() const {
    if (state == State::Matched) return libraryPartId;
    return field("part");
}

const std::string& JPBoardPart::field(const std::string& name) const {
    static const std::string empty;
    const auto it = fields.find(name);
    return it == fields.end() ? empty : it->second;
}

const std::string& JPBoardPart::footprintName() const {
    return !field("footprint").empty() ? field("footprint") : field("package");
}

bool JPBoardPart::samePart(const JPBoardPart& other) const {
    if (footprintName() != other.footprintName()) return false;
    for (const char* f : { "part", "value", "mpn", "manufacturer", "supplierPn" })
        if (field(f) != other.field(f)) return false;
    return true;
}

void JPBoardPart::takeChoice(const JPBoardPart& from) {
    state = from.state;
    libraryPartId = from.libraryPartId;
}

const char* JPBoardPart::stateName(State s) {
    switch (s) {
        case State::Matched: return "matched";
        case State::Unmatched: break;
    }
    return "unmatched";
}

JPBoardPart::State JPBoardPart::stateFrom(const std::string& s) {
    return s == "matched" ? State::Matched : State::Unmatched;   // "local" (a board's own, before): see fromJson
}

JJson JPBoardPart::toJson() const {
    JJson j = JJson::object();
    j["key"] = key;
    JJson f = JJson::object();
    for (const auto& [k, v] : fields) f[k] = v;
    j["fields"] = f;
    JJson r = JJson::object();
    r["state"] = stateName(state);
    if (state == State::Matched) r["libraryId"] = libraryPartId;
    j["resolution"] = r;
    return j;
}

JPBoardPart JPBoardPart::fromJson(const JJson& j) {
    JPBoardPart p;
    if (j["key"].isString()) p.key = j["key"].str();
    if (j["fields"].isObject())
        for (const auto& [k, v] : j["fields"].obj())
            if (v.isString()) p.fields[k] = v.str();
    const JJson& r = j["resolution"];
    const std::string state = r["state"].isString() ? r["state"].str() : std::string();
    p.state = stateFrom(state);
    if (p.state == State::Matched && r["libraryId"].isString()) p.libraryPartId = r["libraryId"].str();
    return p;
}

} // inline namespace jf
