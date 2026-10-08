// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPConfiguration.h"
#include "JPJob.h"
#include "JPStockLot.h"

#include <string>
#include <vector>

inline namespace jf {

// What a job needs against the stock (DESIGN.md, Ready to run): for each part its placements left to place
// (enabled, on the side up, not placed yet), the attrition to allow (the part's own, else what its ledger
// measured, once any were used), how many its open lots hold and where they are kept, and how many short. It
// never stops a run: the feeders hold the material, and a part may be loaded as the run reaches it.
class JPShortages {
public:
    struct Line {
        std::string             partId;
        std::string             partUuid;      // empty: not a library part (the board's own, or not chosen yet)
        int                     needed = 0;
        int                     attrition = 0; // more to allow, at `rate`
        double                  rate = 0;
        enum class Rate { Set, Measured, Unknown } rateFrom = Rate::Unknown;
        long long               inStock = 0;
        std::vector<JPStockLot> lots;          // its open lots
        long long               shortBy = 0;   // needed and attrition, less in stock; 0: enough
    };

    // Every part the job places, the shortest first, then by name.
    static std::vector<Line> of(const JPConfiguration& config, const JPJob& job);
};

} // inline namespace jf
