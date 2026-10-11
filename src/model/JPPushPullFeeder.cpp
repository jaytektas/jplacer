// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPPushPullFeeder.h"

#include "JPLocationJson.h"
#include "JPLocationXml.h"

#include <cctype>
#include <cstdlib>

inline namespace jf {

namespace {

JJson locationJson(const std::optional<JPLocation>& l) { return l ? JPLocationJson::to(*l) : JJson(); }

} // namespace

JPPushPullFeeder::JPPushPullFeeder() {
    snapToAxis = true;
    pipelineType = "ColorKeyed";
}

JPPushPullFeeder JPPushPullFeeder::fromXml(const JPXmlNode& own) {
    JPPushPullFeeder p;
    p.readXml(own);
    if (const std::string* v = own.get("row-location-tolerance-mm"); v && !v->empty()) p.rowLocationToleranceMm = std::strtod(v->c_str(), nullptr);
    if (const std::string* v = own.get("row-z-location-tolerance-mm"); v && !v->empty()) p.rowZLocationToleranceMm = std::strtod(v->c_str(), nullptr);
    if (const std::string* v = own.get("used-as-template")) p.usedAsTemplate = *v == "true";
    // As OpenPnP's hyphenation may also write them (-X, -Y).
    for (const auto& [axis, flag] : { std::pair { "x", &p.calibrateMotionX }, std::pair { "y", &p.calibrateMotionY } }) {
        std::string upper = axis;
        upper[0] = char(std::toupper(static_cast<unsigned char>(upper[0])));
        const std::string* v = own.get(std::string("calibrate-motion-") + axis);
        if (!v) v = own.get("calibrate-motion-" + upper);
        if (v) *flag = *v == "true";
    }
    if (const JPXmlNode* l = own.child("feed-start-location")) p.feedStart = JPLocationXml::from(*l);
    if (const JPXmlNode* l = own.child("feed-mid-1-location")) p.feedMid1 = JPLocationXml::from(*l);
    if (const JPXmlNode* l = own.child("feed-mid-2-location")) p.feedMid2 = JPLocationXml::from(*l);
    if (const JPXmlNode* l = own.child("feed-mid-3-location")) p.feedMid3 = JPLocationXml::from(*l);
    if (const JPXmlNode* l = own.child("feed-end-location")) p.feedEnd = JPLocationXml::from(*l);
    if (const std::string* v = own.get("feed-multiplier"); v && !v->empty()) p.feedMultiplier = std::atoi(v->c_str());
    if (const JPXmlNode* e = own.child("feed-speed-push-1"); e && !e->text.empty()) p.feedSpeedPush1 = std::strtod(e->text.c_str(), nullptr);
    if (const std::string* v = own.get("feed-speed-push-2"); v && !v->empty()) p.feedSpeedPush2 = std::strtod(v->c_str(), nullptr);
    if (const std::string* v = own.get("feed-speed-push-3"); v && !v->empty()) p.feedSpeedPush3 = std::strtod(v->c_str(), nullptr);
    if (const std::string* v = own.get("feed-speed-push-end"); v && !v->empty()) p.feedSpeedPushEnd = std::strtod(v->c_str(), nullptr);
    if (const std::string* v = own.get("feed-speed-pull-3"); v && !v->empty()) p.feedSpeedPull3 = std::strtod(v->c_str(), nullptr);
    if (const std::string* v = own.get("feed-speed-pull-2"); v && !v->empty()) p.feedSpeedPull2 = std::strtod(v->c_str(), nullptr);
    if (const std::string* v = own.get("feed-speed-pull-1"); v && !v->empty()) p.feedSpeedPull1 = std::strtod(v->c_str(), nullptr);
    if (const std::string* v = own.get("feed-speed-pull-0"); v && !v->empty()) p.feedSpeedPull0 = std::strtod(v->c_str(), nullptr);
    if (const std::string* v = own.get("included-push-1")) p.includedPush1 = *v == "true";
    if (const std::string* v = own.get("included-push-2")) p.includedPush2 = *v == "true";
    if (const std::string* v = own.get("included-push-3")) p.includedPush3 = *v == "true";
    if (const std::string* v = own.get("included-push-end")) p.includedPushEnd = *v == "true";
    if (const std::string* v = own.get("included-multi-0")) p.includedMulti0 = *v == "true";
    if (const std::string* v = own.get("included-multi-1")) p.includedMulti1 = *v == "true";
    if (const std::string* v = own.get("included-multi-2")) p.includedMulti2 = *v == "true";
    if (const std::string* v = own.get("included-multi-3")) p.includedMulti3 = *v == "true";
    if (const std::string* v = own.get("included-multi-end")) p.includedMultiEnd = *v == "true";
    if (const std::string* v = own.get("included-pull-0")) p.includedPull0 = *v == "true";
    if (const std::string* v = own.get("included-pull-1")) p.includedPull1 = *v == "true";
    if (const std::string* v = own.get("included-pull-2")) p.includedPull2 = *v == "true";
    if (const std::string* v = own.get("included-pull-3")) p.includedPull3 = *v == "true";
    if (const std::string* v = own.get("delay-0"); v && !v->empty()) p.delay0 = std::atoi(v->c_str());
    if (const std::string* v = own.get("delay-1"); v && !v->empty()) p.delay1 = std::atoi(v->c_str());
    if (const std::string* v = own.get("delay-2"); v && !v->empty()) p.delay2 = std::atoi(v->c_str());
    if (const std::string* v = own.get("delay-3"); v && !v->empty()) p.delay3 = std::atoi(v->c_str());
    if (const std::string* v = own.get("delay-4"); v && !v->empty()) p.delay4 = std::atoi(v->c_str());
    if (const std::string* v = own.get("additive-rotation")) p.additiveRotation = *v == "true";
    if (const std::string* v = own.get("actuator-name")) p.actuatorName = *v;
    if (const std::string* v = own.get("peel-off-actuator-name")) p.peelOffActuatorName = *v;
    if (const std::string* v = own.get("ocr-font-name")) p.ocrFontName = *v;
    if (const std::string* v = own.get("ocr-font-size-pt"); v && !v->empty()) p.ocrFontSizePt = std::strtod(v->c_str(), nullptr);
    if (const std::string* v = own.get("ocr-wrong-part-action")) p.ocrWrongPartAction = *v;
    if (const std::string* v = own.get("ocr-discover-on-job-start")) p.ocrDiscoverOnJobStart = *v == "true";
    if (const std::string* v = own.get("ocr-stop-after-wrong-part")) p.ocrStopAfterWrongPart = *v == "true";
    if (const JPXmlNode* n = own.child("ocr-region")) {
        auto at = [n](const char* name) {
            const JPXmlNode* c = n->child(name);
            return c ? JPLocationXml::from(*c) : JPLocation(JPLengthUnit::Millimeters);
        };
        OcrRegion region;
        region.upperLeft = at("upper-left-corner");
        region.upperRight = at("upper-right-corner");
        region.lowerLeft = at("lower-left-corner");
        const std::string* rectify = n->get("rectify");
        region.rectify = rectify && *rectify == "true";
        if (n->child("offsets")) region.offsets = at("offsets");
        p.ocrRegion = region;
    }
    return p;
}

JPPushPullFeeder JPPushPullFeeder::fromJson(const JJson& j) {
    JPPushPullFeeder p;
    p.readJson(j);
    p.rowLocationToleranceMm = j["rowLocationToleranceMm"].number(4.0);
    p.rowZLocationToleranceMm = j["rowZLocationToleranceMm"].number(1.0);
    p.usedAsTemplate = j["usedAsTemplate"].boolean(false);
    p.calibrateMotionX = j["calibrateMotionX"].boolean(true);
    p.calibrateMotionY = j["calibrateMotionY"].boolean(true);
    p.feedStart = JPLocationJson::from(j["feedStart"]);
    p.feedMid1 = JPLocationJson::from(j["feedMid1"]);
    p.feedMid2 = JPLocationJson::from(j["feedMid2"]);
    p.feedMid3 = JPLocationJson::from(j["feedMid3"]);
    p.feedEnd = JPLocationJson::from(j["feedEnd"]);
    p.feedMultiplier = int(j["feedMultiplier"].number(1));
    p.feedSpeedPush1 = j["feedSpeedPush1"].number(1.0);
    p.feedSpeedPush2 = j["feedSpeedPush2"].number(1.0);
    p.feedSpeedPush3 = j["feedSpeedPush3"].number(1.0);
    p.feedSpeedPushEnd = j["feedSpeedPushEnd"].number(1.0);
    p.feedSpeedPull3 = j["feedSpeedPull3"].number(1.0);
    p.feedSpeedPull2 = j["feedSpeedPull2"].number(1.0);
    p.feedSpeedPull1 = j["feedSpeedPull1"].number(1.0);
    p.feedSpeedPull0 = j["feedSpeedPull0"].number(1.0);
    p.includedPush1 = j["includedPush1"].boolean(false);
    p.includedPush2 = j["includedPush2"].boolean(false);
    p.includedPush3 = j["includedPush3"].boolean(false);
    p.includedPushEnd = j["includedPushEnd"].boolean(true);
    p.includedMulti0 = j["includedMulti0"].boolean(true);
    p.includedMulti1 = j["includedMulti1"].boolean(false);
    p.includedMulti2 = j["includedMulti2"].boolean(false);
    p.includedMulti3 = j["includedMulti3"].boolean(false);
    p.includedMultiEnd = j["includedMultiEnd"].boolean(true);
    p.includedPull0 = j["includedPull0"].boolean(true);
    p.includedPull1 = j["includedPull1"].boolean(false);
    p.includedPull2 = j["includedPull2"].boolean(false);
    p.includedPull3 = j["includedPull3"].boolean(false);
    p.delay0 = int(j["delay0"].number(0));
    p.delay1 = int(j["delay1"].number(0));
    p.delay2 = int(j["delay2"].number(0));
    p.delay3 = int(j["delay3"].number(0));
    p.delay4 = int(j["delay4"].number(0));
    p.additiveRotation = j["additiveRotation"].boolean(true);
    if (j["actuator"].isString()) p.actuatorName = j["actuator"].str();
    if (j["peelOffActuator"].isString()) p.peelOffActuatorName = j["peelOffActuator"].str();
    if (j["ocrFontName"].isString()) p.ocrFontName = j["ocrFontName"].str();
    p.ocrFontSizePt = j["ocrFontSizePt"].number(7.0);
    if (j["ocrWrongPartAction"].isString()) p.ocrWrongPartAction = j["ocrWrongPartAction"].str();
    p.ocrDiscoverOnJobStart = j["ocrDiscoverOnJobStart"].boolean(true);
    p.ocrStopAfterWrongPart = j["ocrStopAfterWrongPart"].boolean(false);
    if (const JJson& o = j["ocrRegion"]; o.isObject()) {
        OcrRegion region;
        region.upperLeft = JPLocationJson::from(o["upperLeft"]);
        region.upperRight = JPLocationJson::from(o["upperRight"]);
        region.lowerLeft = JPLocationJson::from(o["lowerLeft"]);
        region.rectify = o["rectify"].boolean(false);
        if (o["offsets"].isObject()) region.offsets = JPLocationJson::from(o["offsets"]);
        p.ocrRegion = region;
    }
    return p;
}

JJson JPPushPullFeeder::toJson() const {
    JJson j = json();
    j["rowLocationToleranceMm"] = rowLocationToleranceMm;
    j["rowZLocationToleranceMm"] = rowZLocationToleranceMm;
    j["usedAsTemplate"] = usedAsTemplate;
    j["calibrateMotionX"] = calibrateMotionX;
    j["calibrateMotionY"] = calibrateMotionY;
    j["feedStart"] = JPLocationJson::to(feedStart);
    j["feedMid1"] = JPLocationJson::to(feedMid1);
    j["feedMid2"] = JPLocationJson::to(feedMid2);
    j["feedMid3"] = JPLocationJson::to(feedMid3);
    j["feedEnd"] = JPLocationJson::to(feedEnd);
    j["feedMultiplier"] = feedMultiplier;
    j["feedSpeedPush1"] = feedSpeedPush1;
    j["feedSpeedPush2"] = feedSpeedPush2;
    j["feedSpeedPush3"] = feedSpeedPush3;
    j["feedSpeedPushEnd"] = feedSpeedPushEnd;
    j["feedSpeedPull3"] = feedSpeedPull3;
    j["feedSpeedPull2"] = feedSpeedPull2;
    j["feedSpeedPull1"] = feedSpeedPull1;
    j["feedSpeedPull0"] = feedSpeedPull0;
    j["includedPush1"] = includedPush1;
    j["includedPush2"] = includedPush2;
    j["includedPush3"] = includedPush3;
    j["includedPushEnd"] = includedPushEnd;
    j["includedMulti0"] = includedMulti0;
    j["includedMulti1"] = includedMulti1;
    j["includedMulti2"] = includedMulti2;
    j["includedMulti3"] = includedMulti3;
    j["includedMultiEnd"] = includedMultiEnd;
    j["includedPull0"] = includedPull0;
    j["includedPull1"] = includedPull1;
    j["includedPull2"] = includedPull2;
    j["includedPull3"] = includedPull3;
    j["delay0"] = delay0;
    j["delay1"] = delay1;
    j["delay2"] = delay2;
    j["delay3"] = delay3;
    j["delay4"] = delay4;
    j["additiveRotation"] = additiveRotation;
    j["actuator"] = actuatorName;
    j["peelOffActuator"] = peelOffActuatorName;
    j["ocrFontName"] = ocrFontName;
    j["ocrFontSizePt"] = ocrFontSizePt;
    j["ocrWrongPartAction"] = ocrWrongPartAction;
    j["ocrDiscoverOnJobStart"] = ocrDiscoverOnJobStart;
    j["ocrStopAfterWrongPart"] = ocrStopAfterWrongPart;
    if (ocrRegion) {
        JJson o = JJson::object();
        o["upperLeft"] = JPLocationJson::to(ocrRegion->upperLeft);
        o["upperRight"] = JPLocationJson::to(ocrRegion->upperRight);
        o["lowerLeft"] = JPLocationJson::to(ocrRegion->lowerLeft);
        o["rectify"] = ocrRegion->rectify;
        if (ocrRegion->offsets) o["offsets"] = locationJson(ocrRegion->offsets);
        j["ocrRegion"] = o;
    }
    return j;
}

} // inline namespace jf
