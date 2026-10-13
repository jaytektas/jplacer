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

// What a strip feeder (OpenPnP's ReferenceStripFeeder) has of its own: a cut strip of tape laid on the machine,
// the line its parts lie on given by two of its sprocket holes (the one closest to the first part, and another
// further along). Its part pitch and tape width, the tape's kind, how many it has fed and the most it holds (0:
// no most), whether vision finds its holes (and its pipeline: OpenPnP's CvPipeline as OpenPnP keeps it; none:
// the default), how far it goes on before looking again, a parallax view of clear tape; and jplacer's own:
// whether the head camera tunes for it, how much the strip rises along its length (% of the distance from the
// reference hole), and how far along the tape its first part is from the reference hole.
class JPStripFeeder {
public:
    static constexpr const char* kClass = "org.openpnp.machine.reference.feeder.ReferenceStripFeeder";
    static constexpr const char* kKey = "strip";
    // EIA-481's sprocket holes.
    static constexpr double kHoleDiameterMm = 1.5, kHolePitchMm = 4;

    JPLocation               referenceHole { JPLengthUnit::Millimeters }, lastHole { JPLengthUnit::Millimeters };
    JPLength                 partPitch { 4, JPLengthUnit::Millimeters }, tapeWidth { 8, JPLengthUnit::Millimeters };
    std::string              tapeType = "WhitePaper";
    int                      feedCount = 0, maxFeedCount = 0;
    bool                     visionEnabled = false;
    std::optional<JPXmlNode> pipeline;
    JPLength                 extrapolationDistance { 0, JPLengthUnit::Millimeters };
    JPLength                 parallaxDiameter { 0, JPLengthUnit::Millimeters };
    double                   parallaxAngle = 0;
    bool                     autoTune = true;
    double                   zAlongStripPercent = 0;
    // Whether Auto Setup sets the Reference Hole Location's Z (and Z Along Strip %) from the tape's scale: off once
    // the Z is set by hand (typed, or captured with the nozzle), which is truer than the camera's few tenths.
    bool                     autoSetupZ = true;
    // EIA-481's P2 (2 mm), unless auto setup found the first part on a hole (0).
    JPLength                 referenceHoleToPartLinear { 2, JPLengthUnit::Millimeters };
    // Its rotation in tape as EIA-481's (tape 0° with the sprocket holes on top); an older OpenPnP's strip saved
    // as not: its own (one saved before OpenPnP knew of it is brought to it as it is read: JPFeeder::fromXml).
    bool                     standardEia481 = true;

    // OpenPnP's settings of it (its <feeder>'s own, what every feeder has taken out).
    static JPStripFeeder fromXml(const JPXmlNode& own);
    // As jplacer's feeders.json keeps it: {"referenceHole", "lastHole", "partPitchMm", "tapeWidthMm", "tapeType",
    // "feedCount", "maxFeedCount", "visionEnabled", "pipeline", "extrapolationDistanceMm", "parallaxDiameterMm",
    // "parallaxAngle", "autoTune", "zAlongStripPercent", "referenceHoleToPartMm", "standardEia481"}.
    static JPStripFeeder fromJson(const JJson& j);
    JJson toJson() const;
};

} // inline namespace jf
