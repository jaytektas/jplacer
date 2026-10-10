// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPImportSource.h"

#include <string>

inline namespace jf {

// A KiCad board (.kicad_pcb, KiCad 5 on) read as a placement file for the
// CPL and BOM import: a row for each footprint KiCad would put in its
// position file (not one excluded from it), as KiCad's .pos has it (Ref,
// Val, Footprint, PosX, PosY, Rot, Side, in mm from the drill/place file
// origin, Y up), with a DNP column for the parts marked Do Not Populate.
// And the board's footprints, each drawn once: a footprint's SMD pads on its
// copper as its library has them (its placement's rotation taken off, a
// bottom one turned back over; as JPKicadModImporter reads a .kicad_mod),
// by its name without the library's. A name drawn two ways on the board
// (one placed copy edited) is two footprints, the second "Name (2)", and a
// note says which placements take it.
class JPKicadBoardFile {
public:
    // Whether `path` names a KiCad board (its ending).
    static bool is(const std::string& path);
    // `out`'s table, footprints and notes from the file; false (`error`) when it cannot be read or is not
    // a KiCad board.
    static bool read(const std::string& path, JPImportSource& out, std::string& error);
    // The same from the file's text.
    static bool parse(const std::string& text, JPImportSource& out, std::string& error);
};

} // inline namespace jf
