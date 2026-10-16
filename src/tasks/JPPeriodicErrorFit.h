// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "machine/JPPeriodicErrorConfig.h"

#include <string>
#include <vector>

inline namespace jf {

// A drive's periodic error (JPPeriodicErrorConfig) from a board scan: where
// the head camera found footprints against where the board's design puts
// them. The board is fitted as it lies (moved, turned and scaled, as a
// fiducial check fits it) together with the wave along the axis and across
// it, by least squares; a find far from the rest (a part on its pads, a
// pad taken for another) is left out and the fit made again. The wave is how
// far the head was off where it was sent: a find is that much short of
// where the footprint is.
class JPPeriodicErrorFit {
public:
    struct Find {
        double designX = 0, designY = 0;   // on the board (mm)
        double foundX = 0, foundY = 0;     // on the machine, as the camera found it (mm)
        double at = 0;                     // the driven axis's coordinate the camera was at
    };
    // `alongY`: the driven axis is the machine's Y (its error along Y, the twist along X); else X.
    static bool fit(const std::vector<Find>& finds, double periodMm, bool alongY, JPPeriodicErrorConfig::Scan& scan,
                    std::string& why);
    // Fewer than this many finds kept, no fit.
    static constexpr size_t kLeastFinds = 8;
    // A find further off the fit than this many times the finds' typical distance (and at least kLeastOutlierMm)
    // is left out.
    static constexpr double kOutlierSpread = 4.0, kLeastOutlierMm = 0.03;
};

} // inline namespace jf
