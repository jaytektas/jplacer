// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPBlindsFeeder.h"

#include "JPLocationJson.h"
#include "JPLocationXml.h"

#include "openpnp/JPXmlJson.h"

#include <cstdlib>

inline namespace jf {

namespace {

double mm(const JPLength& l) { return l.convertToUnits(JPLengthUnit::Millimeters).value(); }

} // namespace

JPBlindsFeeder JPBlindsFeeder::fromXml(const JPXmlNode& own) {
    JPBlindsFeeder b;
    if (const JPXmlNode* l = own.child("fiducial-1-location")) b.fiducial1 = JPLocationXml::from(*l);
    if (const JPXmlNode* l = own.child("fiducial-2-location")) b.fiducial2 = JPLocationXml::from(*l);
    if (const JPXmlNode* l = own.child("fiducial-3-location")) b.fiducial3 = JPLocationXml::from(*l);
    if (const std::string* v = own.get("normalize")) b.normalize = *v == "true";
    if (const JPXmlNode* l = own.child("tape-length"); l && l->get("value")) b.tapeLengthMm = mm(JPLocationXml::lengthFrom(*l));
    if (const JPXmlNode* l = own.child("feeder-extent"); l && l->get("value")) b.feederExtentMm = mm(JPLocationXml::lengthFrom(*l));
    if (const JPXmlNode* l = own.child("pocket-centerline"); l && l->get("value")) b.pocketCenterlineMm = mm(JPLocationXml::lengthFrom(*l));
    if (const JPXmlNode* l = own.child("pocket-pitch"); l && l->get("value")) b.pocketPitchMm = mm(JPLocationXml::lengthFrom(*l));
    if (const JPXmlNode* l = own.child("pocket-size"); l && l->get("value")) b.pocketSizeMm = mm(JPLocationXml::lengthFrom(*l));
    if (const JPXmlNode* l = own.child("sprocket-pitch"); l && l->get("value")) b.sprocketPitchMm = mm(JPLocationXml::lengthFrom(*l));
    if (const std::string* v = own.get("pocket-count"); v && !v->empty()) b.pocketCount = std::atoi(v->c_str());
    if (const std::string* v = own.get("first-pocket"); v && !v->empty()) b.firstPocket = std::atoi(v->c_str());
    if (const std::string* v = own.get("last-pocket"); v && !v->empty()) b.lastPocket = std::atoi(v->c_str());
    if (const std::string* v = own.get("feed-count"); v && !v->empty()) b.feedCount = std::atoi(v->c_str());
    if (const std::string* v = own.get("feeder-no"); v && !v->empty()) b.feederNo = std::atoi(v->c_str());
    if (const std::string* v = own.get("feeders-total"); v && !v->empty()) b.feedersTotal = std::atoi(v->c_str());
    if (const std::string* v = own.get("feeder-group-name")) b.feederGroupName = *v;
    if (const std::string* v = own.get("vision-enabled")) b.visionEnabled = *v == "true";
    if (const std::string* v = own.get("fid-loc-max-passes"); v && !v->empty()) b.fidLocMaxPasses = std::atoi(v->c_str());
    if (const std::string* v = own.get("fid-loc-tolerance-mm"); v && !v->empty()) b.fidLocToleranceMm = std::strtod(v->c_str(), nullptr);
    if (const std::string* v = own.get("pocket-pos-tolerance-mm"); v && !v->empty()) b.pocketPosToleranceMm = std::strtod(v->c_str(), nullptr);
    if (const std::string* v = own.get("cover-type")) b.coverType = *v;
    if (const std::string* v = own.get("cover-actuation")) b.coverActuation = *v;
    if (const JPXmlNode* l = own.child("edge-open-distance"); l && l->get("value")) b.edgeOpenDistanceMm = mm(JPLocationXml::lengthFrom(*l));
    if (const JPXmlNode* l = own.child("edge-closed-distance"); l && l->get("value")) b.edgeClosedDistanceMm = mm(JPLocationXml::lengthFrom(*l));
    if (const JPXmlNode* l = own.child("push-z-offset"); l && l->get("value")) b.pushZOffsetMm = mm(JPLocationXml::lengthFrom(*l));
    if (const std::string* v = own.get("push-speed"); v && !v->empty()) b.pushSpeed = std::strtod(v->c_str(), nullptr);
    if (const std::string* v = own.get("ocr-action")) b.ocrAction = *v;
    if (const std::string* v = own.get("ocr-font-name")) b.ocrFontName = *v;
    if (const std::string* v = own.get("ocr-text-orientation")) b.ocrTextOrientation = *v;
    if (const JPXmlNode* l = own.child("ocr-margin"); l && l->get("value")) b.ocrMarginMm = mm(JPLocationXml::lengthFrom(*l));
    if (const std::string* v = own.get("ocr-font-size-pt"); v && !v->empty()) b.ocrFontSizePt = std::strtod(v->c_str(), nullptr);
    if (const std::string* v = own.get("max-label-size-mm"); v && !v->empty()) b.maxLabelSizeMm = std::strtod(v->c_str(), nullptr);
    if (const std::string* v = own.get("tape-center-offset-mm"); v && !v->empty()) b.tapeCenterOffsetMm = std::strtod(v->c_str(), nullptr);
    if (const JPXmlNode* p = own.child("pipeline")) b.pipeline = *p;
    return b;
}

JPBlindsFeeder JPBlindsFeeder::fromJson(const JJson& j) {
    JPBlindsFeeder b;
    b.fiducial1 = JPLocationJson::from(j["fiducial1"]);
    b.fiducial2 = JPLocationJson::from(j["fiducial2"]);
    b.fiducial3 = JPLocationJson::from(j["fiducial3"]);
    b.normalize = j["normalize"].boolean(true);
    b.tapeLengthMm = j["tapeLengthMm"].number(0.0);
    b.feederExtentMm = j["feederExtentMm"].number(0.0);
    b.pocketCenterlineMm = j["pocketCenterlineMm"].number(0.0);
    b.pocketPitchMm = j["pocketPitchMm"].number(0.0);
    b.pocketSizeMm = j["pocketSizeMm"].number(0.0);
    b.sprocketPitchMm = j["sprocketPitchMm"].number(4.0);
    b.pocketCount = int(j["pocketCount"].number(0));
    b.firstPocket = int(j["firstPocket"].number(1));
    b.lastPocket = int(j["lastPocket"].number(0));
    b.feedCount = int(j["feedCount"].number(0));
    b.feederNo = int(j["feederNo"].number(0));
    b.feedersTotal = int(j["feedersTotal"].number(0));
    if (j["feederGroup"].isString()) b.feederGroupName = j["feederGroup"].str();
    b.visionEnabled = j["visionEnabled"].boolean(true);
    b.fidLocMaxPasses = int(j["fidLocMaxPasses"].number(3));
    b.fidLocToleranceMm = j["fidLocToleranceMm"].number(0.5);
    b.pocketPosToleranceMm = j["pocketPosToleranceMm"].number(0.1);
    if (j["coverType"].isString()) b.coverType = j["coverType"].str();
    if (j["coverActuation"].isString()) b.coverActuation = j["coverActuation"].str();
    b.edgeOpenDistanceMm = j["edgeOpenDistanceMm"].number(2.0);
    b.edgeClosedDistanceMm = j["edgeClosedDistanceMm"].number(2.0);
    b.pushZOffsetMm = j["pushZOffsetMm"].number(0.25);
    b.pushSpeed = j["pushSpeed"].number(0.1);
    if (j["ocrAction"].isString()) b.ocrAction = j["ocrAction"].str();
    if (j["ocrFontName"].isString()) b.ocrFontName = j["ocrFontName"].str();
    if (j["ocrTextOrientation"].isString()) b.ocrTextOrientation = j["ocrTextOrientation"].str();
    b.ocrMarginMm = j["ocrMarginMm"].number(20.0);
    b.ocrFontSizePt = j["ocrFontSizePt"].number(7.0);
    b.maxLabelSizeMm = j["maxLabelSizeMm"].number(10.0);
    b.tapeCenterOffsetMm = j["tapeCenterOffsetMm"].number(-1.0625);
    if (j["pipeline"].isObject()) b.pipeline = JPXmlJson::node("pipeline", j["pipeline"]);
    return b;
}

JJson JPBlindsFeeder::toJson() const {
    JJson j = JJson::object();
    j["fiducial1"] = JPLocationJson::to(fiducial1);
    j["fiducial2"] = JPLocationJson::to(fiducial2);
    j["fiducial3"] = JPLocationJson::to(fiducial3);
    j["normalize"] = normalize;
    j["tapeLengthMm"] = tapeLengthMm;
    j["feederExtentMm"] = feederExtentMm;
    j["pocketCenterlineMm"] = pocketCenterlineMm;
    j["pocketPitchMm"] = pocketPitchMm;
    j["pocketSizeMm"] = pocketSizeMm;
    j["sprocketPitchMm"] = sprocketPitchMm;
    j["pocketCount"] = pocketCount;
    j["firstPocket"] = firstPocket;
    j["lastPocket"] = lastPocket;
    j["feedCount"] = feedCount;
    j["feederNo"] = feederNo;
    j["feedersTotal"] = feedersTotal;
    j["feederGroup"] = feederGroupName;
    j["visionEnabled"] = visionEnabled;
    j["fidLocMaxPasses"] = fidLocMaxPasses;
    j["fidLocToleranceMm"] = fidLocToleranceMm;
    j["pocketPosToleranceMm"] = pocketPosToleranceMm;
    j["coverType"] = coverType;
    j["coverActuation"] = coverActuation;
    j["edgeOpenDistanceMm"] = edgeOpenDistanceMm;
    j["edgeClosedDistanceMm"] = edgeClosedDistanceMm;
    j["pushZOffsetMm"] = pushZOffsetMm;
    j["pushSpeed"] = pushSpeed;
    j["ocrAction"] = ocrAction;
    j["ocrFontName"] = ocrFontName;
    j["ocrTextOrientation"] = ocrTextOrientation;
    j["ocrMarginMm"] = ocrMarginMm;
    j["ocrFontSizePt"] = ocrFontSizePt;
    j["maxLabelSizeMm"] = maxLabelSizeMm;
    j["tapeCenterOffsetMm"] = tapeCenterOffsetMm;
    if (pipeline) j["pipeline"] = JPXmlJson::from(*pipeline);
    return j;
}

} // inline namespace jf
