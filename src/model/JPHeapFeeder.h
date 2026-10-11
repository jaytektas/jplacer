// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPLocation.h"

#include "openpnp/JPXmlNode.h"

#include <j/config/Json.h>

#include <optional>
#include <string>

inline namespace jf {

// What a heap feeder (OpenPnP's ReferenceHeapFeeder) has of its own: a heap of loose parts a nozzle dips into, the
// parts it brings up dropped into a drop box to be found and picked. Its drop box (JPDropBoxes), how deep the heap
// is below its location and how deep the last feed went, the three moves through the heap, how many failed feeds
// empty the drop box, the vacuum difference that says a part was taken, its feed and training pipelines (each
// OpenPnP's CvPipeline as OpenPnP keeps it; none: the default), and whether it pokes for parts.
class JPHeapFeeder {
public:
    static constexpr const char* kClass = "org.openpnp.machine.reference.feeder.ReferenceHeapFeeder";
    static constexpr const char* kKey = "heap";

    std::string              dropBoxId;
    double                   boxDepth = -25, lastFeedDepth = 0;
    JPLocation               way1 { JPLengthUnit::Millimeters }, way2 { JPLengthUnit::Millimeters }, way3 { JPLengthUnit::Millimeters };
    int                      throwAwayAfterFailedFeeds = 9;
    int                      requiredVacuumDifference = 150;
    std::optional<JPXmlNode> feederPipeline, trainingPipeline;
    bool                     pokeForParts = false;

    // OpenPnP's settings of it (its <feeder>'s own, what every feeder has taken out).
    static JPHeapFeeder fromXml(const JPXmlNode& own);
    // As jplacer's feeders.json keeps it: {"dropBox", "boxDepth", "lastFeedDepth", "way1", "way2", "way3",
    // "throwAwayAfterFailedFeeds", "requiredVacuumDifference", "feederPipeline", "trainingPipeline", "pokeForParts"}.
    static JPHeapFeeder fromJson(const JJson& j);
    JJson toJson() const;
};

} // inline namespace jf
