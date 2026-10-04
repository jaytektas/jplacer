// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPJobProcessorConfig.h"

#include <algorithm>

inline namespace jf {

namespace {

int indexOf(const std::vector<std::string>& keys, const std::string& key, int def) {
    const auto it = std::find(keys.begin(), keys.end(), key);
    return it == keys.end() ? def : int(it - keys.begin());
}

} // namespace

const std::vector<std::string>& JPJobProcessorConfig::jobOrderKeys() {
    static const std::vector<std::string> k { "Part", "PartHeight", "PartBoard", "HeightPartBoard", "BoardPart",
                                              "PickLocation", "PickPlaceLocation", "NozzleTips",
                                              "NozzleTipsByFlexibility", "Unsorted" };
    return k;
}

const std::vector<std::string>& JPJobProcessorConfig::jobOrderNames() {
    static const std::vector<std::string> n { "Part", "Height:Part", "Part:Board", "Height:Part:Board", "Board:Part",
                                              "Pick Locations", "Pick and Place Locations", "Nozzle Tips",
                                              "Nozzle Tips (Inflexible Tips First)", "Unsorted" };
    return n;
}

const std::vector<std::string>& JPJobProcessorConfig::strategyKeys() {
    static const std::vector<std::string> k { "Minimize", "StartAsPlanned", "FullyAsPlanned" };
    return k;
}

const std::vector<std::string>& JPJobProcessorConfig::strategyNames() {
    static const std::vector<std::string> n { "Minimize", "Start As Planned", "Fully As Planned" };
    return n;
}

JJson JPJobProcessorConfig::toJson() const {
    JJson j = JJson::object();
    j["jobOrder"] = jobOrderKeys()[size_t(jobOrder)];
    j["strategy"] = strategyKeys()[size_t(strategy)];
    j["maxVisionRetries"] = maxVisionRetries;
    j["maxPlacementRetries"] = maxPlacementRetries;
    j["feederFaultLimit"] = feederFaultLimit;
    j["feederFaultWindowSize"] = feederFaultWindowSize;
    j["steppingToNextMotion"] = steppingToNextMotion;
    j["optimizeMultipleNozzles"] = optimizeMultipleNozzles;
    j["preRotateAllNozzles"] = preRotateAllNozzles;
    j["fiducialLevel"] = fiducialLevel;
    j["scalingTolerance"] = scalingTolerance;
    j["shearingTolerance"] = shearingTolerance;
    j["boardLocationTolerance"] = boardLocationToleranceMm;
    return j;
}

JPJobProcessorConfig JPJobProcessorConfig::fromJson(const JJson& j) {
    JPJobProcessorConfig c;
    if (!j.isObject()) return c;
    c.jobOrder = JobOrder(indexOf(jobOrderKeys(), j["jobOrder"].str(), int(c.jobOrder)));
    c.strategy = Strategy(indexOf(strategyKeys(), j["strategy"].str(), int(c.strategy)));
    auto num = [&j](const char* k, double def) { return j[k].isNumber() ? j[k].number() : def; };
    auto flag = [&j](const char* k, bool def) { return j[k].isBool() ? j[k].boolean() : def; };
    c.maxVisionRetries = int(num("maxVisionRetries", c.maxVisionRetries));
    c.maxPlacementRetries = int(num("maxPlacementRetries", c.maxPlacementRetries));
    c.feederFaultLimit = int(num("feederFaultLimit", c.feederFaultLimit));
    c.feederFaultWindowSize = int(num("feederFaultWindowSize", c.feederFaultWindowSize));
    c.steppingToNextMotion = flag("steppingToNextMotion", c.steppingToNextMotion);
    c.optimizeMultipleNozzles = flag("optimizeMultipleNozzles", c.optimizeMultipleNozzles);
    c.preRotateAllNozzles = flag("preRotateAllNozzles", c.preRotateAllNozzles);
    c.fiducialLevel = int(num("fiducialLevel", c.fiducialLevel));
    c.scalingTolerance = num("scalingTolerance", c.scalingTolerance);
    c.shearingTolerance = num("shearingTolerance", c.shearingTolerance);
    c.boardLocationToleranceMm = num("boardLocationTolerance", c.boardLocationToleranceMm);
    return c;
}

} // inline namespace jf
