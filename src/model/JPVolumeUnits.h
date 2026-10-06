// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPLengthUnit.h"
#include "JPVolumeUnit.h"

#include <string>
#include <vector>

inline namespace jf {

// Names and short names for each JPVolumeUnit, and the length unit it is the cube of (OpenPnP's VolumeUnit).
struct JPVolumeUnits {
    static const std::vector<JPVolumeUnit>& all();
    static const char* name(JPVolumeUnit u);
    static const char* shortName(JPVolumeUnit u);   // "μl", "mm³"…
    static const char* singularName(JPVolumeUnit u);
    static bool fromName(const std::string& s, JPVolumeUnit& out);
    static JPLengthUnit lengthUnit(JPVolumeUnit u);
    // The cube of the length unit; `preferred`: by its litre name where it has one.
    static JPVolumeUnit fromLengthUnit(JPLengthUnit u, bool preferred = true);
};

} // inline namespace jf
