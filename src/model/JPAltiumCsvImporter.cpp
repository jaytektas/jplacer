// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPAltiumCsvImporter.h"

inline namespace jf {

JPCsvImporter::Patterns JPAltiumCsvImporter::patterns() const {
    Patterns p;
    p.reference = { "DESIGNATOR" };
    p.value     = { "COMMENT" };
    p.package   = { "FOOTPRINT" };
    p.x         = { "REF-X(MM)", "REF-X(MIL)", "CENTER-X(MM)", "CENTER-X(MIL)" };
    p.y         = { "REF-Y(MM)", "REF-Y(MIL)", "CENTER-Y(MM)", "CENTER-Y(MIL)" };
    p.rotation  = { "ROTATION" };
    p.side      = { "LAYER" };
    p.height    = { "HEIGHT(MIL)", "HEIGHT(MM)" };
    p.comment   = { "DESCRIPTION" };
    return p;
}

} // inline namespace jf
