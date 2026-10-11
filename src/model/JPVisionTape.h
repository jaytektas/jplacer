// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPLength.h"
#include "JPLocation.h"

#include "openpnp/JPXmlNode.h"

#include <j/config/Json.h>

#include <optional>
#include <string>

inline namespace jf {

// What a tape feeder calibrated by its sprocket holes has (OpenPnP's push-pull feeder and its Pandaplacer vision
// feeders: JPPushPullFeeder, JPBambooFeeder): the part's rotation in it, two of its holes and whether the pick is
// brought true to them (and snapped square to the machine's axes), the part and feed pitches and how many it has
// fed, its vision pipeline (OpenPnP's CvPipeline as OpenPnP keeps it; none: the default) and its kind
// ("ColorKeyed", "CircularSymmetry"), the precision wanted and how its calibrations went (how many, the sum of
// their errors and of their squares, mm), its calibration tolerances and passes, and when it calibrates
// ("OnFirstUse", "UntilConfident", "OnEachTapeFeed").
class JPVisionTape {
public:
    double                   rotationInFeeder = 0;
    bool                     normalizePickLocation = true, snapToAxis = false;
    JPLocation               hole1 { JPLengthUnit::Millimeters }, hole2 { JPLengthUnit::Millimeters };
    JPLength                 partPitch { 4, JPLengthUnit::Millimeters }, feedPitch { 4, JPLengthUnit::Millimeters };
    int                      feedCount = 0;
    std::optional<JPXmlNode> pipeline;
    std::string              pipelineType = "CircularSymmetry";
    double                   precisionWantedMm = 0.1;
    int                      calibrationCount = 0;
    double                   sumOfErrorsMm = 0, sumOfErrorSquaresMm = 0;
    double                   calibrationToleranceMm = 1.95, sprocketHoleToleranceMm = 0.6, calibrateToleranceMm = 0.3;
    int                      calibrateMaxPasses = 3, calibrateMinStatistic = 2;
    std::string              calibrationTrigger = "UntilConfident";

protected:
    // OpenPnP's settings of it, and jplacer's: {"rotationInFeeder", "normalizePickLocation", "snapToAxis", "hole1",
    // "hole2", "partPitchMm", "feedPitchMm", "feedCount", "pipeline", "pipelineType", "precisionWantedMm",
    // "calibrationCount", "sumOfErrorsMm", "sumOfErrorSquaresMm", "calibrationToleranceMm", "sprocketHoleToleranceMm",
    // "calibrateToleranceMm", "calibrateMaxPasses", "calibrateMinStatistic", "calibrationTrigger"}.
    void readXml(const JPXmlNode& own);
    void readJson(const JJson& j);
    JJson json() const;
};

} // inline namespace jf
