// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPBoardPart.h"
#include "JPConfiguration.h"

#include <string>

inline namespace jf {

// What the library learns from a board part (DESIGN.md, Matching): the only
// two ways it is changed by what boards bring, both chosen.
//  * learn: a library part chosen for a board part keeps that part's names,
//    so the next board that calls it so is matched without asking: the value
//    and footprint as an AKA, the MPN and the supplier's part number as
//    identifiers, the footprint as its package's AKA (each once).
//  * addFrom: a library part made from all a board part says: named by its
//    MPN, else footprint-value, else value (a name taken already: numbered);
//    its package the library's of that footprint (by name or AKA), else one
//    made; its value, description, datasheet, height, identifiers, AKA.
class JPLibraryLearning {
public:
    // `from`: where it was learned (the board), `when` as text.
    static void learn(JPConfiguration& config, JPPart& part, const JPBoardPart& bp, const std::string& from,
                      const std::string& when);
    static JPPart* addFrom(JPConfiguration& config, const JPBoardPart& bp, const std::string& from, const std::string& when);
};

} // inline namespace jf
