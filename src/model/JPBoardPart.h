// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPLibraryFootprint.h"
#include "JPPackage.h"
#include "JPPart.h"

#include <j/config/Json.h>

#include <map>
#include <memory>
#include <string>

inline namespace jf {

// One line of a board's own parts list (DESIGN.md, Board part): what the
// files it came from said about it, kept as they said it, and what it is:
//  * Matched: a part of the library (the parts list every job draws from);
//  * Local: this board's own part (and, where the library has none, its own
//    package), on purpose, kept in the board and never in the library;
//  * Unmatched: only what the files said, not yet anything to place.
// A board's placements each name one of its board parts. Importing makes
// board parts; the library is changed only by choosing to.
class JPBoardPart {
public:
    enum class State { Unmatched, Matched, Local };

    std::string                        key;      // unique within its board ("bp-3")
    std::map<std::string, std::string> fields;   // as imported: "part", "value", "footprint", …
    State                              state = State::Unmatched;
    std::string                        libraryPartId;   // Matched: the library part's id
    std::string                        libraryUuid;     // Matched: the library part's uuid (found by it when renamed)
    // Matched: the board's copy of the library's part and its package as they were when chosen, and the
    // copy's fingerprint (JPLibraryJson): placed by on a machine whose library lacks the part, and compared
    // with the library's to tell when that has changed (JPConfiguration::differs).
    std::shared_ptr<JPPart>            copyPart;
    std::shared_ptr<JPPackage>         copyPackage;
    std::shared_ptr<JPLibraryFootprint> copyFootprint;   // the library footprint its CAD footprint is (its package's)
    std::string                        fingerprint;
    std::shared_ptr<JPPart>            localPart;       // Local: its own part
    std::shared_ptr<JPPackage>         localPackage;    // Local: its own package, when not the library's

    // The part id its placements are placed with: the library part's (matched), its own part's (local), else
    // the name the files gave it (unmatched: a part that is nowhere, as an unknown part id is).
    std::string partId() const;
    // A field as imported, or empty.
    const std::string& field(const std::string& name) const;
    // Its CAD footprint's name: the files' footprint, else their package.
    const std::string& footprintName() const;
    // Whether `other` is the same line of the files: the same footprint and the same part (its name, value,
    // MPN, manufacturer and supplier's part number); a description or a quantity changed does not count.
    bool samePart(const JPBoardPart& other) const;
    // What it was chosen to be (its state, library part and copies, its own part and package) given `from`'s,
    // copies of its own; what the files said is kept.
    void takeChoice(const JPBoardPart& from);

    static const char* stateName(State s);
    static State       stateFrom(const std::string& s);

    JJson              toJson() const;
    static JPBoardPart fromJson(const JJson& j);
};

} // inline namespace jf
