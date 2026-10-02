// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <string>

inline namespace jf {

// Something to show on a camera's live picture where it is on the machine
// (a placement, a fiducial), in the picture's own pixels.
struct JPViewMark {
    double      x = 0, y = 0;     // picture pixels
    double      radius = 0;       // picture pixels; 0: a small marker
    std::string label;
    bool        fiducial = false;
};

} // inline namespace jf
