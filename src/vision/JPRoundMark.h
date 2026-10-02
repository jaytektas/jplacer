// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <string>

inline namespace jf {

// What the round-mark finder found: where (pixels, to a fraction of one), how
// big it measured, how sure it is (0..1), or why it found nothing.
struct JPRoundMark {
    bool        found = false;
    double      x = 0, y = 0;        // centre, pixels
    double      diameter = 0;        // measured, pixels
    double      symmetry = 0;        // the circular-symmetry score at the centre
    double      shape = 0;           // the share of its edge found on one circle, 0..1
    double      confidence = 0;      // 0..1: agreement of size and shape with what was expected
    std::string why;                 // when not found
};

} // inline namespace jf
