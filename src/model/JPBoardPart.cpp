// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPBoardPart.h"

#include "JPLibraryJson.h"

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
    // Copies of its own: a part or package changed in one revision is not changed in another.
    auto own = [](const auto& p) { return p ? std::make_shared<std::decay_t<decltype(*p)>>(*p) : nullptr; };
    state = from.state;
    libraryPartId = from.libraryPartId;
    libraryUuid = from.libraryUuid;
    copyPart = own(from.copyPart);
    copyPackage = own(from.copyPackage);
    copyFootprint = own(from.copyFootprint);
    fingerprint = from.fingerprint;
    localPart = own(from.localPart);
    localPackage = own(from.localPackage);
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
    if (state == State::Matched) {
        r["libraryId"] = libraryPartId;
        if (!libraryUuid.empty()) r["libraryUuid"] = libraryUuid;
        if (copyPart) {
            JJson copy = JJson::object();
            copy["part"] = JPLibraryJson::part(*copyPart);
            if (copyPackage) copy["package"] = JPLibraryJson::package(*copyPackage);
            if (copyFootprint) copy["footprint"] = JPLibraryJson::footprint(*copyFootprint);
            r["copy"] = copy;
            r["fingerprint"] = fingerprint;
        }
    }
    if (state == State::Local) {
        // As the library keeps them (JPLibraryJson): all of a part (its value, identifiers, packaging), not only
        // OpenPnP's.
        if (localPart) r["part"] = JPLibraryJson::part(*localPart);
        if (localPackage) r["package"] = JPLibraryJson::package(*localPackage);
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
    if (p.state == State::Matched) {
        if (r["libraryId"].isString()) p.libraryPartId = r["libraryId"].str();
        if (r["libraryUuid"].isString()) p.libraryUuid = r["libraryUuid"].str();
        if (r["copy"]["part"].isObject()) {
            p.copyPart = std::make_shared<JPPart>(JPLibraryJson::part(r["copy"]["part"]));
            if (r["copy"]["package"].isObject()) p.copyPackage = std::make_shared<JPPackage>(JPLibraryJson::package(r["copy"]["package"]));
            if (r["copy"]["footprint"].isObject())
                p.copyFootprint = std::make_shared<JPLibraryFootprint>(JPLibraryJson::footprint(r["copy"]["footprint"]));
            if (r["fingerprint"].isString()) p.fingerprint = r["fingerprint"].str();
        }
    }
    if (p.state == State::Local) {
        // OpenPnP's part and package as XML in JSON (a board saved before: {"tag": …}), else the library's form.
        auto openpnp = [](const JJson& o) { return o["tag"].isString(); };
        if (r["part"].isObject())
            p.localPart = std::make_shared<JPPart>(openpnp(r["part"]) ? JPPart::fromXml(JPXmlJson::element(r["part"]))
                                                                      : JPLibraryJson::part(r["part"]));
        if (r["package"].isObject())
            p.localPackage = std::make_shared<JPPackage>(openpnp(r["package"]) ? JPPackage::fromXml(JPXmlJson::element(r["package"]))
                                                                               : JPLibraryJson::package(r["package"]));
        if (!p.localPart) p.state = State::Unmatched;   // a local part with nothing of its own is not one
        // One made before a board's own part took its value: the value its files wrote.
        else if (p.localPart->value.empty()) p.localPart->value = p.field("value");
    }
    return p;
}

} // inline namespace jf
