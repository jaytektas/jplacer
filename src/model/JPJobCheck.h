// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPConfiguration.h"
#include "JPJob.h"

#include <string>
#include <vector>

inline namespace jf {

// The job's data checked before a run (DESIGN.md, Ready to run), as one list naming what to do, each thing once
// with every placement or part it is about:
//  * Stop: the run would refuse (a board with an id twice; a placement whose part is not chosen or not known;
//    a part with no package, or no nozzle tip on the machine that fits it);
//  * Check: worth putting right first (placements not verified, parts of unknown height, parts with no
//    footprint to draw or check against);
//  * Note: to know (parts no feeder holds yet, asked for as the run gets to them; parts short of stock, of those
//    whose stock is kept).
// The placements are those a run places: enabled, not placed, their side up on an enabled board.
class JPJobCheck {
public:
    struct Item {
        enum class Level { Stop, Check, Note };
        Level                    level = Level::Note;
        std::string              what;    // "No part chosen"
        std::vector<std::string> which;   // the placements ("Brd1⇒R1") or parts it is about
        std::string              fix;     // where it is put right
    };

    // `machineTipIds`: the nozzle tips the machine's nozzles can load.
    static std::vector<Item> of(JPConfiguration& config, const JPJob& job, const std::vector<std::string>& machineTipIds);
    static const char* levelName(Item::Level l);
    // Whether any item stops the run, or asks to be checked.
    static bool stops(const std::vector<Item>& items);
    static bool asks(const std::vector<Item>& items);
};

} // inline namespace jf
