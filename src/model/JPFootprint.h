// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPLengthUnit.h"

#include "openpnp/JPXmlElement.h"
#include "openpnp/JPXmlNode.h"

#include <string>
#include <vector>

inline namespace jf {

// A package's footprint, as OpenPnP's Footprint: its pads and body, in its
// units, and the numbers its generators (Dual, Quad, BGA) make pads from.
class JPFootprint {
public:
    struct Pad {
        std::string name;
        double      x = 0, y = 0, width = 0, height = 0;
        double      rotation = 0;
        bool        mark = false;      // pin 1, as OpenPnP marks it
        double      roundness = 0;     // percent; negative: rounded on one side only
    };
    enum class Generator { Dual, Quad, Bga, Kicad };

    JPLengthUnit     units = JPLengthUnit::Millimeters;
    std::vector<Pad> pads;
    double bodyWidth = 0, bodyHeight = 0;
    double outerDimension = 0, innerDimension = 0;
    int    padCount = 0;
    double padPitch = 0, padAcross = 0, padRoundness = 0;

    // Pad 1's mark moved to `pad` (or taken off it, when it had it).
    void toggleMark(size_t pad);
    // Pads made from the generator's numbers and added; false (and why)
    // when the numbers do not suit it. Kicad pads come from a .kicad_mod file
    // (JPKicadModImporter), not from here.
    bool generate(Generator type, std::string& error);

    static JPFootprint fromXml(const JPXmlElement& e);
    JPXmlNode toXml() const;
};

} // inline namespace jf
