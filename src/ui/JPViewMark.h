// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <string>
#include <utility>
#include <vector>

inline namespace jf {

// Something to show on a camera's live picture where it is on the machine
// (a placement, a fiducial), in the picture's own pixels: a ring, or the
// outlines of a footprint's pads with a dot on pin 1.
struct JPViewMark {
    double      x = 0, y = 0;     // picture pixels
    double      radius = 0;       // picture pixels; 0: a small marker
    std::string label;
    bool        fiducial = false;
    std::vector<std::vector<std::pair<double, double>>> outlines;   // picture pixels, each closed
    bool        hasPin1 = false;
    double      pin1X = 0, pin1Y = 0, pin1Radius = 0;               // picture pixels
};

} // inline namespace jf
