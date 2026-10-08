// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPBoardPart.h"

#include "openpnp/JPXmlJson.h"

inline namespace jf {

std::string JPBoardPart::partId() const {
    if (state == State::Matched) return libraryPartId;
    if (state == State::Local && localPart) return localPart->id;
    return field("part");
}

const std::string& JPBoardPart::field(const std::string& name) const {
    static const std::string empty;
    const auto it = fields.find(name);
    return it == fields.end() ? empty : it->second;
}

const char* JPBoardPart::stateName(State s) {
    switch (s) {
        case State::Matched: return "matched";
        case State::Local:   return "local";
        case State::Unmatched: break;
    }
    return "unmatched";
}

JPBoardPart::State JPBoardPart::stateFrom(const std::string& s) {
    if (s == "matched") return State::Matched;
    if (s == "local") return State::Local;
    return State::Unmatched;
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
    if (state == State::Local) {
        if (localPart) r["part"] = JPXmlJson::from(localPart->toXml());
        if (localPackage) r["package"] = JPXmlJson::from(localPackage->toXml());
    }
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
    p.state = stateFrom(r["state"].isString() ? r["state"].str() : std::string());
    if (p.state == State::Matched && r["libraryId"].isString()) p.libraryPartId = r["libraryId"].str();
    if (p.state == State::Local) {
        if (r["part"].isObject()) p.localPart = std::make_shared<JPPart>(JPPart::fromXml(JPXmlJson::element(r["part"])));
        if (r["package"].isObject())
            p.localPackage = std::make_shared<JPPackage>(JPPackage::fromXml(JPXmlJson::element(r["package"])));
        if (!p.localPart) p.state = State::Unmatched;   // a local part with nothing of its own is not one
    }
    return p;
}

} // inline namespace jf
