// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPBoardPart.h"
#include "JPConfiguration.h"

#include <string>

inline namespace jf {

// What the library learns from a board part (DESIGN.md, Matching): the only
// ways it is changed by what boards bring: the name a board gives a part
// whenever one is linked to it (learnName), and two chosen.
//  * learn: a library part chosen for a board part keeps that part's names,
//    so the next board that calls it so is matched without asking: the value
//    and footprint as an AKA, the MPN and the supplier's part number as
//    identifiers, the footprint as its package's AKA (each once).
//  * addFrom: a library part made from all a board part says: known by a
//    new GUID, named (its Name, which the user may change) by its MPN, else
//    footprint-value, else value (a name taken already: numbered);
//    its package the library's of that footprint (by name or AKA), else one
//    made; its value, description, datasheet, height, identifiers, AKA.
//  * describe: what a board part says of the part itself (its description,
//    datasheet, MPN and supplier's part number) given to a part where it has
//    none: one made from it (addFrom, an import's parts made for the
//    library), and the library's part an import is matched to, as the import
//    is taken (what the part has kept).
class JPLibraryLearning {
public:
    // The name a board gives a part (its value and footprint) kept as the library part's AKA, when it has it not
    // already: done whenever a board part is linked to it, so a part renamed is still found by what boards call
    // it. True when it was added.
    static bool learnName(JPPart& part, const JPBoardPart& bp, const std::string& from, const std::string& when);
    // `from`: where it was learned (the board), `when` as text.
    static void learn(JPConfiguration& config, JPPart& part, const JPBoardPart& bp, const std::string& from,
                      const std::string& when);
    // Its description, datasheet, MPN and supplier's part number, each where the board part gives it and the part
    // has none (no identifier of that kind).
    static void describe(JPPart& part, const JPBoardPart& bp);
    static JPPart* addFrom(JPConfiguration& config, const JPBoardPart& bp, const std::string& from, const std::string& when);
};

} // inline namespace jf
