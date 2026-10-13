// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPEasyEdaPart.h"

#include <optional>
#include <string>
#include <vector>

inline namespace jf {

class JPConfiguration;

// Packages given what LCSC knows of them (JPLcscLookup), by the LCSC part number of a part of each: their pads
// (a package with none), their dimensions from the part's 3D model (a body or height not given), or both. What to
// ask for is worked out on the screen's thread, each lookup made off it, and what it found given on it again.
class JPLcscFetch {
public:
    struct Ask {
        std::string packageId, packageName, lcsc;
        bool        pads = false, dimensions = false;
    };
    struct Answer {
        bool                  found = false;   // the part (its footprint) found
        JPEasyEdaPart         part;
        std::optional<double> heightMm;        // its 3D model's, where it was asked for and found
        std::vector<std::string> problems;     // what went wrong, each said
        bool                  partKept = false, modelKept = false;   // found kept (lcsc/), not asked for
    };

    // Each of `packageIds` (empty: every package) that wants what is asked for and has a part with an LCSC part
    // number; `without` counts those that want it but have no such part.
    static std::vector<Ask> asksFor(const JPConfiguration& config, const std::vector<std::string>& packageIds, bool pads,
                                    bool dimensions, int& without);
    // The lookup (it waits on the network: not on the screen's thread).
    static Answer fetch(const std::string& cacheDir, const Ask& ask);
    // What it found given to the package (and its parts what they lack), each with where it came from (the package's
    // padsSource, bodySource, heightSource); what was given, said ("3 pads, body, height"), empty: nothing.
    static std::string give(JPConfiguration& config, const Ask& ask, const Answer& answer);
};

} // inline namespace jf
