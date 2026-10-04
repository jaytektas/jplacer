// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPFootprint.h"

#include <string>
#include <vector>

inline namespace jf {

// A KiCad footprint (.kicad_mod) read for its pads, as OpenPnP's
// KicadModImporter reads it: the SMD pads on the top copper, Y turned to
// OpenPnP's way up, rectangles square, circles and ovals round, rounded
// rectangles by their ratio; other shapes left out. A pad written over
// several lines (as newer KiCad writes them) is read whole.
class JPKicadModImporter {
public:
    // False (and why) when the file cannot be read.
    static bool read(const std::string& path, std::vector<JPFootprint::Pad>& pads, std::string& error);
    // The same from the file's text.
    static std::vector<JPFootprint::Pad> parse(const std::string& text);
};

} // inline namespace jf
