// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <string>

inline namespace jf {

// What a placement's part was chosen to be (the part picker): a library part
// (and, `learn`, the library remembering what the files call it), a new
// library part made from what the files said, the board's own part made from
// it, or left to be chosen; for every placement of its board part, or for the
// one placement alone.
struct JPPartChoice {
    enum class Kind { Library, AddToLibrary, BoardsOwn, ToBeChosen };
    Kind        kind = Kind::Library;
    std::string libraryId;        // Library: the part's id
    bool        learn = false;    // Library: its value, footprint, MPN and supplier PN kept with it (JPLibraryLearning)
    bool        onlyThis = false; // the one placement alone (split from the others of its board part)
};

} // inline namespace jf
