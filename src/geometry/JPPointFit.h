// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPAffine2D.h"

#include <optional>
#include <vector>

inline namespace jf {

// The map that best takes points to where they were seen, by least squares:
// how fiducials found on a board say where the board is.
class JPPointFit {
public:
    struct Pair {
        double fromX = 0, fromY = 0;   // where it is in its own frame (on the board)
        double toX = 0, toY = 0;       // where it was seen (on the machine)
    };
    struct Result {
        JPAffine2D map;
        double rms = 0;   // how far the points sit from where the map puts them
    };

    // Moved and turned only, applied after `base` (which may mirror): the
    // board is rigid. Needs one pair (moved only, turn kept from base) or two.
    static std::optional<Result> rigid(const std::vector<Pair>& pairs, const JPAffine2D& base);
    // Any affine map: takes up a machine whose axes are not quite square or
    // to scale as well. Needs three pairs, not in a line.
    static std::optional<Result> affine(const std::vector<Pair>& pairs);
};

} // inline namespace jf
