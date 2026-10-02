// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <string>

inline namespace jf {

// One designator on a board, as the CAD placed it: where (board millimetres,
// seen from the side it is on as the CAD shows that side), turned how far,
// which side, and what (footprint and value strings as the CAD wrote them).
struct JPPlacement {
    enum class Side { Top, Bottom };

    std::string designator;
    double      x = 0, y = 0;
    double      rotationDeg = 0;
    Side        side = Side::Top;
    std::string footprint;
    std::string value;
    bool        fiducial = false;   // a mark to find the board by, not a part to place
    double      fiducialMm = 0;     // a fiducial's copper diameter, where its footprint says (0: not said)
};

} // inline namespace jf
