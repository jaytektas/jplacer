// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "job/JPBoard.h"

#include <string>
#include <vector>

inline namespace jf {

// A pick-and-place (centroid, CPL, .pos) file from a PCB tool, as CSV: one
// row per designator. Columns are found by their headings, whatever the tool
// calls them (Designator / Ref, Mid X / PosX / Center-X, Layer / Side,
// Rotation / Rot, Footprint / Package, Value / Comment), with or without
// units in the numbers ("12.5mm", "500mil") or the heading ("PosX(mm)").
// Fiducials are told by designator (FID...) or footprint (FIDUCIAL...).
// Nothing goes into the library: the board is the file's.
class JPCplImporter {
public:
    static bool read(const std::string& path, JPBoard& board, std::vector<std::string>& notes, std::string& error);
    // The same from the file's text (for a file already read).
    static bool parse(const std::string& text, JPBoard& board, std::vector<std::string>& notes, std::string& error);
};

} // inline namespace jf
