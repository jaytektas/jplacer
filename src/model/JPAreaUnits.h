// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPAreaUnit.h"
#include "JPLengthUnit.h"

#include <string>
#include <vector>

inline namespace jf {

// Names and short names for each JPAreaUnit, and the length unit it is the square of (OpenPnP's AreaUnit).
struct JPAreaUnits {
    static const std::vector<JPAreaUnit>& all();
    static const char* name(JPAreaUnit u);
    static const char* shortName(JPAreaUnit u);   // "mm²"…
    static const char* singularName(JPAreaUnit u);
    static bool fromName(const std::string& s, JPAreaUnit& out);
    static JPLengthUnit lengthUnit(JPAreaUnit u);
    static JPAreaUnit fromLengthUnit(JPLengthUnit u);
};

} // inline namespace jf
