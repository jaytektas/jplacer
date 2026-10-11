// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPVisionTape.h"

#include "JPLocationJson.h"
#include "JPLocationXml.h"

#include "openpnp/JPXmlJson.h"

#include <cstdlib>

inline namespace jf {

namespace {

double mm(const JPLength& l) { return l.convertToUnits(JPLengthUnit::Millimeters).value(); }

} // namespace

void JPVisionTape::readXml(const JPXmlNode& own) {
    if (const std::string* v = own.get("rotation-in-feeder"); v && !v->empty()) rotationInFeeder = std::strtod(v->c_str(), nullptr);
    if (const std::string* v = own.get("normalize-pick-location")) normalizePickLocation = *v == "true";
    if (const std::string* v = own.get("snap-to-axis")) snapToAxis = *v == "true";
    if (const JPXmlNode* l = own.child("hole-1-location")) hole1 = JPLocationXml::from(*l);
    if (const JPXmlNode* l = own.child("hole-2-location")) hole2 = JPLocationXml::from(*l);
    if (const JPXmlNode* l = own.child("part-pitch"); l && l->get("value")) partPitch = JPLocationXml::lengthFrom(*l);
    if (const JPXmlNode* l = own.child("feed-pitch"); l && l->get("value")) feedPitch = JPLocationXml::lengthFrom(*l);
    if (const std::string* v = own.get("feed-count"); v && !v->empty()) feedCount = std::atoi(v->c_str());
    if (const JPXmlNode* p = own.child("pipeline")) pipeline = *p;
    if (const std::string* v = own.get("pipeline-type")) pipelineType = *v;
    if (const JPXmlNode* l = own.child("precision-wanted"); l && l->get("value")) precisionWantedMm = mm(JPLocationXml::lengthFrom(*l));
    if (const std::string* v = own.get("calibration-count"); v && !v->empty()) calibrationCount = std::atoi(v->c_str());
    if (const JPXmlNode* l = own.child("sum-of-errors"); l && l->get("value")) sumOfErrorsMm = mm(JPLocationXml::lengthFrom(*l));
    if (const JPXmlNode* l = own.child("sum-of-error-squares"); l && l->get("value")) sumOfErrorSquaresMm = mm(JPLocationXml::lengthFrom(*l));
    if (const std::string* v = own.get("calibration-tolerance-mm"); v && !v->empty()) calibrationToleranceMm = std::strtod(v->c_str(), nullptr);
    if (const std::string* v = own.get("sprocket-hole-tolerance-mm"); v && !v->empty()) sprocketHoleToleranceMm = std::strtod(v->c_str(), nullptr);
    if (const std::string* v = own.get("calibrate-tolerance-mm"); v && !v->empty()) calibrateToleranceMm = std::strtod(v->c_str(), nullptr);
    if (const std::string* v = own.get("calibrate-max-passes"); v && !v->empty()) calibrateMaxPasses = std::atoi(v->c_str());
    if (const std::string* v = own.get("calibrate-min-statistic"); v && !v->empty()) calibrateMinStatistic = std::atoi(v->c_str());
    if (const std::string* v = own.get("calibration-trigger")) calibrationTrigger = *v;
}

void JPVisionTape::readJson(const JJson& j) {
    rotationInFeeder = j["rotationInFeeder"].number(0.0);
    normalizePickLocation = j["normalizePickLocation"].boolean(true);
    snapToAxis = j["snapToAxis"].boolean(snapToAxis);
    hole1 = JPLocationJson::from(j["hole1"]);
    hole2 = JPLocationJson::from(j["hole2"]);
    partPitch = JPLength(j["partPitchMm"].number(4.0), JPLengthUnit::Millimeters);
    feedPitch = JPLength(j["feedPitchMm"].number(4.0), JPLengthUnit::Millimeters);
    feedCount = int(j["feedCount"].number(0));
    if (j["pipeline"].isObject()) pipeline = JPXmlJson::node("pipeline", j["pipeline"]);
    if (j["pipelineType"].isString()) pipelineType = j["pipelineType"].str();
    precisionWantedMm = j["precisionWantedMm"].number(0.1);
    calibrationCount = int(j["calibrationCount"].number(0));
    sumOfErrorsMm = j["sumOfErrorsMm"].number(0.0);
    sumOfErrorSquaresMm = j["sumOfErrorSquaresMm"].number(0.0);
    calibrationToleranceMm = j["calibrationToleranceMm"].number(1.95);
    sprocketHoleToleranceMm = j["sprocketHoleToleranceMm"].number(0.6);
    calibrateToleranceMm = j["calibrateToleranceMm"].number(0.3);
    calibrateMaxPasses = int(j["calibrateMaxPasses"].number(3));
    calibrateMinStatistic = int(j["calibrateMinStatistic"].number(2));
    if (j["calibrationTrigger"].isString()) calibrationTrigger = j["calibrationTrigger"].str();
}

JJson JPVisionTape::json() const {
    JJson j = JJson::object();
    j["rotationInFeeder"] = rotationInFeeder;
    j["normalizePickLocation"] = normalizePickLocation;
    j["snapToAxis"] = snapToAxis;
    j["hole1"] = JPLocationJson::to(hole1);
    j["hole2"] = JPLocationJson::to(hole2);
    j["partPitchMm"] = partPitch.convertToUnits(JPLengthUnit::Millimeters).value();
    j["feedPitchMm"] = feedPitch.convertToUnits(JPLengthUnit::Millimeters).value();
    j["feedCount"] = feedCount;
    if (pipeline) j["pipeline"] = JPXmlJson::from(*pipeline);
    j["pipelineType"] = pipelineType;
    j["precisionWantedMm"] = precisionWantedMm;
    j["calibrationCount"] = calibrationCount;
    j["sumOfErrorsMm"] = sumOfErrorsMm;
    j["sumOfErrorSquaresMm"] = sumOfErrorSquaresMm;
    j["calibrationToleranceMm"] = calibrationToleranceMm;
    j["sprocketHoleToleranceMm"] = sprocketHoleToleranceMm;
    j["calibrateToleranceMm"] = calibrateToleranceMm;
    j["calibrateMaxPasses"] = calibrateMaxPasses;
    j["calibrateMinStatistic"] = calibrateMinStatistic;
    j["calibrationTrigger"] = calibrationTrigger;
    return j;
}

} // inline namespace jf
