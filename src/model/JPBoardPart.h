// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

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
//  * Matched: a part of the library, by its id (and uuid, to find it when renamed);
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
    std::string                        libraryUuid;     // Matched: the library part's uuid (found by it when renamed)

    // What a board saved before the library was the only place of parts kept of this one: its own part and
    // package (`own`), or its copy of the library's part, package and footprint. Read from such a file only, to
    // be put in the library as the board is opened (JPConfiguration::adoptFormer), then let go.
    struct Former {
        bool                                own = false;
        std::shared_ptr<JPPart>             part;
        std::shared_ptr<JPPackage>          package;
        std::shared_ptr<JPLibraryFootprint> footprint;
    };
    std::optional<Former> former;

    // The part id its placements are placed with: the library part's (matched), else the name the files gave
    // it (unmatched: a part that is nowhere, as an unknown part id is).
    std::string partId() const;
    // A field as imported, or empty.
    const std::string& field(const std::string& name) const;
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
