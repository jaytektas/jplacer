// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPHeapFeeder.h"

#include "JPLocationJson.h"
#include "JPLocationXml.h"

#include "openpnp/JPXmlJson.h"

#include <cstdlib>

inline namespace jf {

JPHeapFeeder JPHeapFeeder::fromXml(const JPXmlNode& own) {
    JPHeapFeeder h;
    auto text = [&own](const char* k) {
        const std::string* v = own.get(k);
        return v ? *v : std::string();
    };
    h.dropBoxId = text("drop-box-id");
    if (const std::string v = text("box-depth"); !v.empty()) h.boxDepth = std::strtod(v.c_str(), nullptr);
    if (const std::string v = text("last-feed-depth"); !v.empty()) h.lastFeedDepth = std::strtod(v.c_str(), nullptr);
    if (const JPXmlNode* l = own.child("way-1")) h.way1 = JPLocationXml::from(*l);
    if (const JPXmlNode* l = own.child("way-2")) h.way2 = JPLocationXml::from(*l);
    if (const JPXmlNode* l = own.child("way-3")) h.way3 = JPLocationXml::from(*l);
    if (const std::string v = text("throw-away-drop-box-content-after-failed-feeds"); !v.empty())
        h.throwAwayAfterFailedFeeds = std::atoi(v.c_str());
    if (const std::string v = text("required-vacuum-difference"); !v.empty()) h.requiredVacuumDifference = std::atoi(v.c_str());
    if (const JPXmlNode* p = own.child("feeder-pipeline")) h.feederPipeline = *p;
    if (const JPXmlNode* p = own.child("training-pipeline")) h.trainingPipeline = *p;
    // OpenPnP keeps it as an element.
    if (const JPXmlNode* p = own.child("poke-for-parts")) h.pokeForParts = p->text == "true";
    return h;
}

JPHeapFeeder JPHeapFeeder::fromJson(const JJson& j) {
    JPHeapFeeder h;
    h.dropBoxId = j["dropBox"].str();
    h.boxDepth = j["boxDepth"].number(-25.0);
    h.lastFeedDepth = j["lastFeedDepth"].number(0.0);
    h.way1 = JPLocationJson::from(j["way1"]);
    h.way2 = JPLocationJson::from(j["way2"]);
    h.way3 = JPLocationJson::from(j["way3"]);
    h.throwAwayAfterFailedFeeds = int(j["throwAwayAfterFailedFeeds"].number(9));
    h.requiredVacuumDifference = int(j["requiredVacuumDifference"].number(150));
    if (j["feederPipeline"].isObject()) h.feederPipeline = JPXmlJson::node("feeder-pipeline", j["feederPipeline"]);
    if (j["trainingPipeline"].isObject()) h.trainingPipeline = JPXmlJson::node("training-pipeline", j["trainingPipeline"]);
    h.pokeForParts = j["pokeForParts"].boolean(false);
    return h;
}

JJson JPHeapFeeder::toJson() const {
    JJson j = JJson::object();
    j["dropBox"] = dropBoxId;
    j["boxDepth"] = boxDepth;
    j["lastFeedDepth"] = lastFeedDepth;
    j["way1"] = JPLocationJson::to(way1);
    j["way2"] = JPLocationJson::to(way2);
    j["way3"] = JPLocationJson::to(way3);
    j["throwAwayAfterFailedFeeds"] = throwAwayAfterFailedFeeds;
    j["requiredVacuumDifference"] = requiredVacuumDifference;
    if (feederPipeline) j["feederPipeline"] = JPXmlJson::from(*feederPipeline);
    if (trainingPipeline) j["trainingPipeline"] = JPXmlJson::from(*trainingPipeline);
    j["pokeForParts"] = pokeForParts;
    return j;
}

} // inline namespace jf
