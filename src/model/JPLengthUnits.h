// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPLengthUnit.h"

#include <string>

inline namespace jf {

// Names, short names and millimetres for each JPLengthUnit.
struct JPLengthUnits {
    static const char* name(JPLengthUnit u);
    static const char* shortName(JPLengthUnit u);   // "mm", "\"", "mil"…
    static const char* singularName(JPLengthUnit u);
    // From a file's name; false when it names none.
    static bool fromName(const std::string& s, JPLengthUnit& out);
    static double toMillimeters(JPLengthUnit u);   // millimetres in one of `u`
};

} // inline namespace jf
