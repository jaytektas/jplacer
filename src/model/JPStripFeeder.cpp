// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPStripFeeder.h"

#include "JPLocationJson.h"
#include "JPLocationXml.h"

#include "openpnp/JPXmlJson.h"

#include <cstdlib>

inline namespace jf {

namespace {

constexpr JPLengthUnit kMm = JPLengthUnit::Millimeters;

double mm(const JPLength& l) { return l.convertToUnits(kMm).value(); }

} // namespace

JPStripFeeder JPStripFeeder::fromXml(const JPXmlNode& own) {
    JPStripFeeder s;
    auto text = [&own](const char* k) {
        const std::string* v = own.get(k);
        return v ? *v : std::string();
    };
    auto length = [&own](const char* k, JPLength& out) {
        if (const JPXmlNode* c = own.child(k); c && c->get("value")) out = JPLocationXml::lengthFrom(*c);
    };
    if (const JPXmlNode* l = own.child("reference-hole-location")) s.referenceHole = JPLocationXml::from(*l);
    if (const JPXmlNode* l = own.child("last-hole-location")) s.lastHole = JPLocationXml::from(*l);
    length("part-pitch", s.partPitch);
    length("tape-width", s.tapeWidth);
    if (const std::string t = text("tape-type"); !t.empty()) s.tapeType = t;
    if (const std::string c = text("feed-count"); !c.empty()) s.feedCount = std::atoi(c.c_str());
    if (const std::string c = text("max-feed-count"); !c.empty()) s.maxFeedCount = std::atoi(c.c_str());
    s.visionEnabled = text("vision-enabled") == "true";
    if (const JPXmlNode* p = own.child("pipeline")) s.pipeline = *p;
    length("extrapolation-distance", s.extrapolationDistance);
    length("parallax-diameter", s.parallaxDiameter);
    // OpenPnP keeps the parallax angle as an element.
    if (const JPXmlNode* a = own.child("parallax-angle"); a && !a->text.empty()) s.parallaxAngle = std::strtod(a->text.c_str(), nullptr);
    if (const std::string t = text("auto-tune"); !t.empty()) s.autoTune = t == "true";
    if (const std::string z = text("z-along-strip-percent"); !z.empty()) s.zAlongStripPercent = std::strtod(z.c_str(), nullptr);
    length("reference-hole-to-part-linear", s.referenceHoleToPartLinear);
    s.standardEia481 = text("standard-eia-481") != "false";
    return s;
}

JPStripFeeder JPStripFeeder::fromJson(const JJson& j) {
    JPStripFeeder s;
    s.referenceHole = JPLocationJson::from(j["referenceHole"]);
    s.lastHole = JPLocationJson::from(j["lastHole"]);
    s.partPitch = JPLength(j["partPitchMm"].number(4.0), kMm);
    s.tapeWidth = JPLength(j["tapeWidthMm"].number(8.0), kMm);
    if (j["tapeType"].isString()) s.tapeType = j["tapeType"].str();
    s.feedCount = int(j["feedCount"].number(0));
    s.maxFeedCount = int(j["maxFeedCount"].number(0));
    s.visionEnabled = j["visionEnabled"].boolean(false);
    if (j["pipeline"].isObject()) s.pipeline = JPXmlJson::node("pipeline", j["pipeline"]);
    s.extrapolationDistance = JPLength(j["extrapolationDistanceMm"].number(0.0), kMm);
    s.parallaxDiameter = JPLength(j["parallaxDiameterMm"].number(0.0), kMm);
    s.parallaxAngle = j["parallaxAngle"].number(0.0);
    s.autoTune = j["autoTune"].boolean(true);
    s.zAlongStripPercent = j["zAlongStripPercent"].number(0.0);
    s.referenceHoleToPartLinear = JPLength(j["referenceHoleToPartMm"].number(2.0), kMm);
    s.standardEia481 = j["standardEia481"].boolean(true);
    return s;
}

JJson JPStripFeeder::toJson() const {
    JJson j = JJson::object();
    j["referenceHole"] = JPLocationJson::to(referenceHole);
    j["lastHole"] = JPLocationJson::to(lastHole);
    j["partPitchMm"] = mm(partPitch);
    j["tapeWidthMm"] = mm(tapeWidth);
    j["tapeType"] = tapeType;
    j["feedCount"] = feedCount;
    j["maxFeedCount"] = maxFeedCount;
    j["visionEnabled"] = visionEnabled;
    if (pipeline) j["pipeline"] = JPXmlJson::from(*pipeline);
    j["extrapolationDistanceMm"] = mm(extrapolationDistance);
    j["parallaxDiameterMm"] = mm(parallaxDiameter);
    j["parallaxAngle"] = parallaxAngle;
    j["autoTune"] = autoTune;
    j["zAlongStripPercent"] = zAlongStripPercent;
    j["referenceHoleToPartMm"] = mm(referenceHoleToPartLinear);
    j["standardEia481"] = standardEia481;
    return j;
}

} // inline namespace jf
