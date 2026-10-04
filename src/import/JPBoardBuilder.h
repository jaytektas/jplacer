// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "job/JPBoard.h"
#include "job/JPBoardFrame.h"
#include "job/JPSource.h"

#include <set>
#include <string>
#include <vector>

inline namespace jf {

// A board made from its sources: the pick-and-place file read as its CAD tool
// writes it and brought into the board's one frame (seen from the top,
// origin at the frame's), then each placement joined by its designator to
// its BOM line, where there is a BOM.
//
// What the BOM says fills what the pick-and-place file leaves empty. Where
// both say something different about a designator (its value, MPN,
// manufacturer, supplier numbers, footprint, or whether it is placed), it is
// a disagreement: the pick-and-place file's stands unless the person has
// chosen the BOM's for it. A designator the BOM lists that the file does not,
// and a placement with no BOM line, are listed too. A designator twice in
// the file refuses the file: placements are matched by designator.
class JPBoardBuilder {
public:
    struct Disagreement {
        std::string designator;
        std::string field;
        std::string cpl, bom;
        bool        takeBom = false;
        // "designator|field", as `takeBom` names them.
        std::string key() const { return designator + "|" + field; }
    };
    struct Result {
        JPBoard                   board;
        std::vector<std::string>  notes;
        std::vector<Disagreement> disagreements;
        std::vector<std::string>  bomOnly;     // in the BOM, not in the file
        std::vector<std::string>  noBomLine;   // parts in the file the BOM does not list
    };

    // `takeBom`: the disagreements (by key) to settle the BOM's way.
    static bool build(const std::vector<JPSource>& sources, const JPBoardFrame& frame, const std::set<std::string>& takeBom,
                      Result& out, std::string& error);
};

} // inline namespace jf
