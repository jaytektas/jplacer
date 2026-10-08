// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPPlacement.h"

#include "JPLocationJson.h"

#include "JPLocationXml.h"
#include "JPSides.h"
#include "JPXmlValues.h"

inline namespace jf {

using V = JPXmlValues;

namespace {
constexpr const char* kVersion = "1.4";
}

const char* JPPlacement::errorHandlingName(ErrorHandling e) {
    switch (e) {
        case ErrorHandling::Default: return "Default";
        case ErrorHandling::Alert:   return "Alert";
        case ErrorHandling::Defer:   return "Defer";
    }
    return "Default";
}

JPPlacement::ErrorHandling JPPlacement::errorHandlingFrom(const std::string& s) {
    if (s == "Alert") return ErrorHandling::Alert;
    if (s == "Defer") return ErrorHandling::Defer;
    return ErrorHandling::Default;
}

JPPlacement JPPlacement::fromXml(const JPXmlElement& e) {
    JPPlacement p;
    p.id = e.attr("id");
    p.side = JPSides::fromName(e.attr("side"));
    p.partId = e.attr("part-id");
    p.enabled = V::boolean(e, "enabled", true);
    p.rank = V::integer(e, "rank");
    const std::string type = e.attr("type");
    if (type == "Fiducial") p.type = Type::Fiducial;
    else p.type = Type::Placement;
    if (type == "Ignore") p.enabled = false;
    if (const JPXmlElement* l = e.child("location")) p.location = JPLocationXml::from(*l);
    if (const JPXmlElement* c = e.child("comments")) p.comments = V::text(*c);
    if (const JPXmlElement* h = e.child("error-handling")) p.errorHandling = errorHandlingFrom(V::text(*h));
    return p;
}

JPXmlNode JPPlacement::toXml() const {
    JPXmlNode n("placement");
    n.attr("version", kVersion).attr("side", JPSides::name(side)).attr("id", id);
    if (!partId.empty()) n.attr("part-id", partId);
    n.attr("type", typeName(type)).attr("enabled", V::boolean(enabled));
    if (rank != 0) n.attr("rank", std::to_string(rank));
    n.add(JPLocationXml::to("location", location));
    if (comments) n.add(JPXmlNode("comments")).text = *comments;
    n.add(JPXmlNode("error-handling")).text = errorHandlingName(errorHandling);
    return n;
}

JPPlacement JPPlacement::fromJson(const JJson& j) {
    JPPlacement p;
    if (j["id"].isString()) p.id = j["id"].str();
    p.side = JPSides::fromName(j["side"].isString() ? j["side"].str() : std::string());
    if (j["part"].isString()) p.boardPart = j["part"].str();
    p.type = j["type"].isString() && j["type"].str() == "Fiducial" ? Type::Fiducial : Type::Placement;
    p.enabled = j["enabled"].boolean(true);
    p.rank = int(j["rank"].number());
    p.location = JPLocationJson::from(j["location"]);
    if (j["comments"].isString()) p.comments = j["comments"].str();
    if (j["errorHandling"].isString()) p.errorHandling = errorHandlingFrom(j["errorHandling"].str());
    if (j["cadRotation"].isNumber()) p.cadRotation = j["cadRotation"].number();
    if (j["verified"]["by"].isString()) {
        p.verified.by = j["verified"]["by"].str();
        if (j["verified"]["when"].isString()) p.verified.when = j["verified"]["when"].str();
    }
    return p;
}

JJson JPPlacement::toJson() const {
    JJson j = JJson::object();
    j["id"] = id;
    j["side"] = JPSides::name(side);
    if (!boardPart.empty()) j["part"] = boardPart;
    j["type"] = typeName(type);
    j["enabled"] = enabled;
    if (rank != 0) j["rank"] = rank;
    j["location"] = JPLocationJson::to(location);
    if (comments) j["comments"] = *comments;
    if (errorHandling != ErrorHandling::Default) j["errorHandling"] = errorHandlingName(errorHandling);
    if (cadRotation) j["cadRotation"] = *cadRotation;
    if (!verified.by.empty()) {
        JJson v = JJson::object();
        v["by"] = verified.by;
        v["when"] = verified.when;
        j["verified"] = v;
    }
    return j;
}

} // inline namespace jf
