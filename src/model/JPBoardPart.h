// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPFields.h"
#include "JPLibraryFootprint.h"
#include "JPPackage.h"
#include "JPPart.h"

#include <j/config/Json.h>

#include <map>
#include <memory>
#include <optional>
#include <string>

inline namespace jf {

// One line of a board's parts list (DESIGN.md, Board part): what the
// files it came from said about it, kept as they said it, and the library
// part it is (the library is the only place parts and packages are kept):
//  * Matched: a part of the library, by its id (a GUID: renaming the part changes nothing here);
//  * Unmatched: only what the files said, not yet a part to place.
// A board's placements each name one of its board parts. Importing makes
// board parts (and, asked to, the library parts the library lacks).
class JPBoardPart {
public:
    enum class State { Unmatched, Matched };

    std::string                        key;      // unique within its board ("bp-3")
    std::map<std::string, std::string> fields;   // as imported: "part", "value", "footprint", …
    State                              state = State::Unmatched;
    std::string                        libraryPartId;   // Matched: the library part's id

    // The part id its placements are placed with: the library part's (matched), else the name the files gave
    // it (unmatched: a part that is nowhere, as an unknown part id is).
    std::string partId() const;
    // A field as imported, or empty.
    const std::string& field(const std::string& name) const;
    // What the files said of the fields added to parts ("part:<key>") or to packages ("package:<key>"), each read
    // as its field's type (as the import kept it: a length in millimetres).
    JPFieldValues added(const JPFields& fields, JPFieldSet::Of of) const;
    // Its CAD footprint's name: the files' footprint, else their package.
    const std::string& footprintName() const;
    // Whether `other` is the same line of the files: the same footprint and the same part (its name, value,
    // MPN, manufacturer and supplier's part number); a description or a quantity changed does not count.
    bool samePart(const JPBoardPart& other) const;
    // What it was chosen to be (its state and library part) given `from`'s; what the files said is kept.
    void takeChoice(const JPBoardPart& from);

    static const char* stateName(State s);
    static State       stateFrom(const std::string& s);

    JJson              toJson() const;
    static JPBoardPart fromJson(const JJson& j);
};

} // inline namespace jf
