// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPConfiguration.h"

#include <memory>
#include <string>
#include <vector>

inline namespace jf {

// Every part and package jplacer has open, wherever it lives: the library's,
// and each open board's (its own, and its copies of the library's it uses),
// each saying where it is and how it stands to the library. What the Parts
// and Packages tabs list, and what a part or package is chosen from.
class JPCatalog {
public:
    enum class Status {
        Library,          // the library's own
        Matched,          // a board's copy of the library's part (or package), as the library has it
        LibraryChanged,   // a board's copy, the library's changed since it was taken
        Own,              // a board's own, the library has none
        ToBeChosen,       // a board's part not chosen yet: only what its files said
    };
    // Each holds what it shows (its part or package, its board) so a row stays good while the board's parts
    // change under it (an import adding to them), until the views read the rows again.
    struct Part {
        std::shared_ptr<JPPart>  held;           // none: to be chosen
        std::shared_ptr<JPBoard> board;          // none: the library's
        std::string              boardPartKey;   // its board part (JPBoardPart::key)
        Status                   status = Status::Library;
        std::string              name;           // its id as shown (a board's without the board's name before it)
        JPPart*      part() const { return held.get(); }
        JPBoardPart* boardPart() const { return board ? board->part(boardPartKey) : nullptr; }
    };
    struct Package {
        std::shared_ptr<JPPackage> held;
        std::shared_ptr<JPBoard>   board;
        std::string                boardPartKey;   // the first board part using it
        Status                     status = Status::Library;
        std::string                name;
        JPPackage*   package() const { return held.get(); }
        JPBoardPart* boardPart() const { return board ? board->part(boardPartKey) : nullptr; }
    };

    // The library's, in its order, then each open board's in its parts' order.
    static std::vector<Part>    parts(const JPConfiguration& config);
    // The library's, then each open board's: one a package id a board (its parts share it).
    static std::vector<Package> packages(const JPConfiguration& config);

    // "" (the library's: its Source says so), "Matched", "Library changed", "Own", "To be chosen".
    static const char* statusName(Status s);
    // What a status means, for its tooltip.
    static const char* statusTip(Status s);
    // Where it lives: "Library", or the board's name.
    static std::string source(const JPBoard* board);
    static std::string source(const std::shared_ptr<JPBoard>& board) { return source(board.get()); }
    // Whether its row is edited where it is shown: the library's, and a board's own.
    static bool editable(Status s) { return s == Status::Library || s == Status::Own; }

    // A board's own part (and its package, where the library has none of that name) copied into the library,
    // under its name without the board's; the board keeps its own. False (`error`) when the library has a part of
    // that name already.
    static bool addToLibrary(JPConfiguration& config, const Part& entry, std::string& error);
    // A board's copy of a library part taken again from the library as it is now.
    static void updateFromLibrary(const JPConfiguration& config, const Part& entry);
    // A board's own package edited: every other own part of the board with a package of that id given the same.
    static void shareEdit(JPBoard& board, const JPPackage& edited);

    // Whether `entry` is what `words` asks for: every word (case not minded) found in its name, value,
    // description, package, footprint, MPN or the names the library learned for it, a value word as a value
    // ("100n" finds 0.1u), and "cap", "res", "ind", "led", "fid" its kind's standard footprint names
    // (C_, R_, L_, LED_, Fiducial). How well: 0 not at all, higher the better (a word at a name's start
    // before one inside it).
    static int matches(const JPConfiguration& config, const Part& entry, const std::string& words);
    static int matches(const Package& entry, const std::string& words);
};

} // inline namespace jf
