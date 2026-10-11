// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPLocation.h"

#include "openpnp/JPXmlNode.h"

#include <j/config/Json.h>

#include <optional>
#include <string>

inline namespace jf {

// What a blinds feeder (OpenPnP's BlindsFeeder) has of its own: one tape in a 3D-printed holder that holds several,
// the holder found by its three fiducials (its frame: JPBlindsFeeders), each tape's pockets under a cover pushed open
// and shut by a nozzle. Lengths in mm; the choices by OpenPnP's names. What the holder's feeders share (its
// fiducials, size, normalising, vision, group and OCR) is given to the others as it is set (JPBlindsFeeders).
class JPBlindsFeeder {
public:
    static constexpr const char* kClass = "org.openpnp.machine.reference.feeder.BlindsFeeder";
    static constexpr const char* kKey = "blinds";
    static constexpr const char* kDefaultGroup = "Default";

    // The holder: its fiducials, whether its frame is made square and 2 mm true, its tape length and extent.
    JPLocation fiducial1 { JPLengthUnit::Millimeters }, fiducial2 { JPLengthUnit::Millimeters }, fiducial3 { JPLengthUnit::Millimeters };
    bool       normalize = true;
    double     tapeLengthMm = 0, feederExtentMm = 0;
    // The tape: its centerline across the holder, pocket pitch and size, sprocket pitch; its pockets, the first and
    // last to pick, how many fed.
    double     pocketCenterlineMm = 0, pocketPitchMm = 0, pocketSizeMm = 0, sprocketPitchMm = 4;
    int        pocketCount = 0, firstPocket = 1, lastPocket = 0, feedCount = 0;
    // Its place on the holder (numbered across it) and the holder's group.
    int        feederNo = 0, feedersTotal = 0;
    std::string feederGroupName = kDefaultGroup;
    // Vision: whether the fiducials are found, the pipeline (OpenPnP's CvPipeline as OpenPnP keeps it; none: the
    // default), its passes and tolerance, and the pocket position tolerance.
    bool                     visionEnabled = true;
    std::optional<JPXmlNode> pipeline;
    int                      fidLocMaxPasses = 3;
    double                   fidLocToleranceMm = 0.5, pocketPosToleranceMm = 0.1;
    // The cover: its kind ("BlindsCover", "PushCover", "NoCover"), when it is opened ("None", "CheckOpen",
    // "OpenOnFirstUse", "OpenOnJobStart"), the edges the nozzle pushes it by, how far down and how fast it pushes.
    std::string coverType = "BlindsCover", coverActuation = "OpenOnJobStart";
    double      edgeOpenDistanceMm = 2, edgeClosedDistanceMm = 2, pushZOffsetMm = 0.25, pushSpeed = 0.1;
    // OCR of the tape's label: what to do ("None", "Stop", "ChangePart", "CheckPart"), how far from the tape it is
    // looked for, its font and size, which way its text reads ("AwayFromTape", "TowardsTape"), the largest label,
    // and the tape's centre from its label.
    std::string ocrAction = "None", ocrFontName = "Liberation Mono", ocrTextOrientation = "AwayFromTape";
    double      ocrMarginMm = 20, ocrFontSizePt = 7, maxLabelSizeMm = 10, tapeCenterOffsetMm = -1.0625;

    // Fiducial `n` (1 to 3).
    JPLocation&       fiducial(int n) { return n == 1 ? fiducial1 : n == 2 ? fiducial2 : fiducial3; }
    const JPLocation& fiducial(int n) const { return n == 1 ? fiducial1 : n == 2 ? fiducial2 : fiducial3; }

    // OpenPnP's settings of it (its <feeder>'s own, what every feeder has taken out).
    static JPBlindsFeeder fromXml(const JPXmlNode& own);
    // As jplacer's feeders.json keeps it: each setting by its name here (lengths "…Mm"), the pipeline in OpenPnP's
    // shape (JPXmlJson).
    static JPBlindsFeeder fromJson(const JJson& j);
    JJson toJson() const;
};

} // inline namespace jf
