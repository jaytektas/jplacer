// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPFootprint.h"

#include <string>
#include <vector>

inline namespace jf {

// A footprint in the library (DESIGN.md, Footprint): a land pattern of a
// package, its own entity, one package having several (an R0603's "nominal",
// "reflow", a CAD library's). Its pads (and pin 1), in the package's footprint
// geometry; the names CAD files give it ("R_0603_1608Metric", "RESC1608X55N"),
// by which a board's footprint is found, and through it the package; its zero
// rotation against jplacer's (IPC-7351: pin 1 top left), what a CAD tool's
// placements are turned by to be jplacer's; where it came from (a .kicad_mod).
// The package's own footprint (JPPackage::footprint) is what vision measures
// the part by; a library footprint may be made from it, or made it.
struct JPLibraryFootprint {
    std::string              uuid;
    std::string              name;            // unique in its package
    std::string              packageId;
    JPFootprint              geometry;
    std::vector<std::string> cadNames;
    double                   zeroRotationDeg = 0;
    std::string              source;
};

} // inline namespace jf
