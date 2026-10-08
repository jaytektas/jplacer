// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPFootprint.h"
#include "JPVisionCompositing.h"

#include "openpnp/JPXmlElement.h"
#include "openpnp/JPXmlNode.h"

#include <optional>
#include <string>
#include <vector>

inline namespace jf {

// A package, as OpenPnP's Package: its id, description, tape
// specification, vacuum levels, footprint, the nozzle tips that can pick
// it, and the vision settings it uses (by id; empty: inherited).
class JPPackage {
public:
    std::string                id;
    std::string                uuid;   // the library's for good (JPUuid); empty until it is in the library
    std::optional<std::string> description;
    std::optional<std::string> tapeSpecification;
    double                     pickVacuumLevel = 0;
    double                     placeBlowOffLevel = 0;
    JPFootprint                footprint;
    std::vector<std::string>   compatibleNozzleTipIds;
    std::string                bottomVisionId;
    std::string                fiducialVisionId;
    // Made when first wanted, as OpenPnP makes it (and written from then on).
    std::optional<JPVisionCompositing> visionCompositing;

    static JPPackage fromXml(const JPXmlElement& e);
    JPXmlNode toXml() const;
};

} // inline namespace jf
