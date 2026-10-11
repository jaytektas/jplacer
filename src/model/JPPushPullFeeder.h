// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPVisionTape.h"

#include <optional>
#include <string>

inline namespace jf {

// A push-pull feeder (OpenPnP's ReferencePushPullFeeder): a tape moved on by a lever its actuator pushes through
// five places (start, three in between, end) and pulls back, each move at its own speed and in the push, the pull
// or the extra moves of a feed of several pitches (Multi), with a pause after each; the lever's motion calibrated
// in X and Y by vision or not, its rotation additive or not; a peel-off actuator. What a vision-calibrated tape
// has, and that; whether it is a template other feeders clone from; how far apart feeders of a row may lie; the
// tape's label read by OCR (its font and size, its region, what to do with a wrong part, whether to read it at job
// start and stop after a wrong one).
class JPPushPullFeeder : public JPVisionTape {
public:
    static constexpr const char* kClass = "org.openpnp.machine.reference.feeder.ReferencePushPullFeeder";
    static constexpr const char* kKey = "pushPull";

    // Where its label is read (OpenPnP's RegionOfInterest): three corners about the camera's centre, rectified or
    // not, and (optionally) where it is from the vision location.
    struct OcrRegion {
        JPLocation                upperLeft { JPLengthUnit::Millimeters }, upperRight { JPLengthUnit::Millimeters },
                                  lowerLeft { JPLengthUnit::Millimeters };
        bool                      rectify = false;
        std::optional<JPLocation> offsets;
    };

    double      rowLocationToleranceMm = 4, rowZLocationToleranceMm = 1;
    bool        usedAsTemplate = false, calibrateMotionX = true, calibrateMotionY = true;
    JPLocation  feedStart { JPLengthUnit::Millimeters }, feedMid1 { JPLengthUnit::Millimeters }, feedMid2 { JPLengthUnit::Millimeters },
                feedMid3 { JPLengthUnit::Millimeters }, feedEnd { JPLengthUnit::Millimeters };
    int         feedMultiplier = 1;
    double      feedSpeedPush1 = 1, feedSpeedPush2 = 1, feedSpeedPush3 = 1, feedSpeedPushEnd = 1;
    double      feedSpeedPull3 = 1, feedSpeedPull2 = 1, feedSpeedPull1 = 1, feedSpeedPull0 = 1;
    bool        includedPush1 = false, includedPush2 = false, includedPush3 = false, includedPushEnd = true;
    bool        includedMulti0 = true, includedMulti1 = false, includedMulti2 = false, includedMulti3 = false, includedMultiEnd = true;
    bool        includedPull0 = true, includedPull1 = false, includedPull2 = false, includedPull3 = false;
    int         delay0 = 0, delay1 = 0, delay2 = 0, delay3 = 0, delay4 = 0;
    bool        additiveRotation = true;
    std::string actuatorName, peelOffActuatorName;
    std::string ocrFontName = "Liberation Mono";
    double      ocrFontSizePt = 7;
    std::optional<OcrRegion> ocrRegion;
    // "None", "SwapFeeders", "SwapOrCreate", "ChangePart", "ChangePartAndClone".
    std::string ocrWrongPartAction = "SwapOrCreate";
    bool        ocrDiscoverOnJobStart = true, ocrStopAfterWrongPart = false;

    JPPushPullFeeder();

    // OpenPnP's settings of it (its <feeder>'s own, what every feeder has taken out).
    static JPPushPullFeeder fromXml(const JPXmlNode& own);
    // As jplacer's feeders.json keeps it: a vision tape's, and each of these by its name here ("ocrRegion"
    // {"upperLeft", "upperRight", "lowerLeft", "rectify", "offsets"}).
    static JPPushPullFeeder fromJson(const JJson& j);
    JJson toJson() const;
};

} // inline namespace jf
