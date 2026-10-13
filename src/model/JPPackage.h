// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPFieldValues.h"
#include "JPFootprint.h"
#include "JPLength.h"
#include "JPVisionCompositing.h"

#include "openpnp/JPXmlElement.h"
#include "openpnp/JPXmlNode.h"

#include <optional>
#include <string>
#include <vector>

inline namespace jf {

// A package, as OpenPnP's Package: its id, description, height, vacuum
// levels, footprint, the nozzle tips that can pick it, and the vision
// settings it uses (by id; empty: inherited). Not OpenPnP's tape
// specification (a label typed): the tape a part comes on is its own
// (JPPart's Packaging); one read from OpenPnP's files is left behind.
class JPPackage {
public:
    std::string                id;      // a GUID (JPUuid), for good: what parts know it by; empty until it is in the library
    std::string                label;       // what it is called where it is shown (its Name), unique in the library
    std::optional<std::string> description;
    // How high its parts are, where a part says nothing of its own (0: not known).
    JPLength                   height { 0, JPLengthUnit::Millimeters };
    double                     pickVacuumLevel = 0;
    double                     placeBlowOffLevel = 0;
    JPFootprint                footprint;
    std::vector<std::string>   compatibleNozzleTipIds;
    std::string                bottomVisionId;
    std::string                fiducialVisionId;
    JPFieldValues              custom;   // its values of the fields added to packages
    // Where its footprint's pads, its body (the footprint's width and length) and its height came from ("LCSC C1634
    // (EasyEDA)", "KiCad R_0603.kicad_mod", "Dual generator"); empty: typed in, or not recorded.
    std::string                padsSource, bodySource, heightSource;
    // Made when first wanted, as OpenPnP makes it (and written from then on).
    std::optional<JPVisionCompositing> visionCompositing;

    // What it is called where it is shown: its Name.
    const std::string& shownName() const { return label; }

    static JPPackage fromXml(const JPXmlElement& e);
    JPXmlNode toXml() const;
};

} // inline namespace jf
