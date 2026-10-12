// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPEasyEdaPart.h"
#include "JPPackage.h"
#include "JPPart.h"

#include <string>

inline namespace jf {

// A part looked up by LCSC's part number in EasyEDA's component lookup (JPEasyEdaPart), and what is taken from it:
// a package's pads, and a part's description, MPN, manufacturer and height where it has none. Each answer is kept
// (a file a part, in `cacheDir`), so a part is asked for once and found again offline. Only the part number leaves
// the machine.
class JPLcscLookup {
public:
    static constexpr const char* kCacheFolder = "lcsc";   // in the configuration's folder
    static constexpr int         kTimeoutMs = 15000;

    // The part: kept, else asked for (and kept). It waits for the answer: not on the screen's thread. False (and
    // why) when there is no answer, or no such part, or no footprint for it.
    static bool find(const std::string& cacheDir, const std::string& lcsc, JPEasyEdaPart& out, std::string& why);
    // The package's pads made the part's (in the package's units), its old ones gone.
    static void giveFootprint(JPPackage& package, const JPEasyEdaPart& part);
    // What the part says, given to `part` where it has none: description, MPN (with its manufacturer), height.
    static void describe(JPPart& part, const JPEasyEdaPart& from);
    // A part's LCSC part number (its supplier part number, LCSC's or one written as LCSC writes them); empty: none.
    static std::string lcscOf(const JPPart& part);
};

} // inline namespace jf
