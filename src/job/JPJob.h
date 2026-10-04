// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPBoard.h"
#include "JPBoardFrame.h"
#include "JPSource.h"

#include "library/JPPartsStore.h"

#include <j/config/Json.h>

#include <string>
#include <vector>

inline namespace jf {

// A job: one board, as read from its CAD files, and the job's own parts,
// packages and footprints (copies of the library's, or made in the job), so
// it opens and runs the same whatever the library does later. Kept as JSON
// in a .jpjob file wherever the person saves it.
struct JPJob {
    static constexpr const char* kExtension = "jpjob";

    JPBoard               board;
    JPPartsStore          parts;
    std::vector<JPSource> sources;   // the files the board was read from
    JPBoardFrame          frame;     // its origin and outline
    // Where the pick-and-place file and the BOM disagree, those settled the
    // BOM's way ("designator|field", JPBoardBuilder::Disagreement::key).
    std::vector<std::string> bomChoices;

    // A save writes a new file and puts it in place only once it is whole.
    bool save(const std::string& path, std::string& error) const;
    static bool load(const std::string& path, JPJob& out, std::string& error);

    JJson toJson() const;
    static bool fromJson(const JJson& j, JPJob& out, std::string& error);
};

} // inline namespace jf
