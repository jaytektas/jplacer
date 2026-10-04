// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPFootprint.h"

#include <string>

inline namespace jf {

// A KiCad footprint file (.kicad_mod, KiCad 5's `module` and 6 onward's
// `footprint`) read into a footprint: its name, its pads (copper ones;
// mechanical holes without a number are left out), and its body from the
// fabrication layer's outline. KiCad's Y runs down; jplacer's runs up, so Y
// is turned over and the footprint looks as KiCad draws it. Pin 1 is the pad
// numbered "1", else "A1".
class JPKicadFootprint {
public:
    static bool parse(const std::string& text, JPFootprint& out, std::string& error);
    static bool read(const std::string& path, JPFootprint& out, std::string& error);
};

} // inline namespace jf
