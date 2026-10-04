// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPOrigin.h"

#include <j/config/Json.h>

#include <string>
#include <vector>

inline namespace jf {

// The physical body a part comes in (SOIC-8, 0603), as its datasheet gives
// it: the footprint it uses, its size and height, and everything about
// picking, moving, looking at and placing it, set once. A part says nothing
// physical; this is where those facts are, held once. 0 (or empty) is not
// set: the machine's default fills it.
//
// `names` are the CAD footprint names and supplier package names it has been
// matched to ("R_0603_1608Metric", "R0603", "0603"); a name belongs to one
// package only.
struct JPPackage {
    std::string              id;
    std::string              name;
    std::string              footprintId;   // empty: none yet (not placeable)
    double                   length = 0, width = 0, height = 0;
    std::vector<std::string> tips;          // compatible nozzle tips by name, preferred first
    double                   speed = 0;     // 0..1 of the machine's
    int                      pickRetries = -1;   // -1: the machine's
    std::vector<std::string> names;
    int                      revision = 1;
    JPOrigin                 origin;

    bool hasName(const std::string& n) const;

    static JPPackage fromJson(const JJson& j);
    JJson toJson() const;
};

} // inline namespace jf
