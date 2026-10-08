// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <string>

inline namespace jf {

// What a placement's part was chosen to be (the part picker): a library part,
// the board's own part made from what the files said, or left to be chosen;
// for every placement of its board part, or for the one placement alone.
struct JPPartChoice {
    enum class Kind { Library, BoardsOwn, ToBeChosen };
    Kind        kind = Kind::Library;
    std::string libraryId;        // Library: the part's id
    bool        onlyThis = false; // the one placement alone (split from the others of its board part)
};

} // inline namespace jf
