// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPReferenceCsvImporter.h"

inline namespace jf {

JPCsvImporter::Patterns JPReferenceCsvImporter::patterns() const {
    Patterns p;
    p.reference = { "DESIGNATOR", "PART", "COMPONENT", "REFDES", "REF" };
    p.value     = { "VALUE", "VAL", "COMMENT", "COMP_VALUE" };
    p.package   = { "FOOTPRINT", "PACKAGE", "PATTERN", "COMP_PACKAGE" };
    p.x         = { "X", "X (MM)", "REF X", "POSX", "REF-X(MM)", "REF-X(MIL)", "SYM_X" };
    p.y         = { "Y", "Y (MM)", "REF Y", "POSY", "REF-Y(MM)", "REF-Y(MIL)", "SYM_Y" };
    p.rotation  = { "ROTATION", "ROT", "ROTATE", "SYM_ROTATE" };
    p.side      = { "LAYER", "SIDE", "TB", "SYM_MIRROR" };
    p.height    = { "HEIGHT", "HEIGHT(MIL)", "HEIGHT(MM)" };
    p.comment   = { "ADDCOMMENT" };
    return p;
}

} // inline namespace jf
