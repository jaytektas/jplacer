// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPPlacement.h"

#include <string>
#include <vector>

inline namespace jf {

// A PCB design: its placements, parts and fiducials, both sides.
struct JPBoard {
    std::string              name;
    std::vector<JPPlacement> placements;

    // The fiducials on one side.
    std::vector<const JPPlacement*> fiducials(JPPlacement::Side side) const {
        std::vector<const JPPlacement*> out;
        for (const JPPlacement& p : placements)
            if (p.fiducial && p.side == side) out.push_back(&p);
        return out;
    }
    const JPPlacement* find(const std::string& designator) const {
        for (const JPPlacement& p : placements)
            if (p.designator == designator) return &p;
        return nullptr;
    }
};

} // inline namespace jf
