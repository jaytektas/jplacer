// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPCadTool.h"

#include <string>

inline namespace jf {

// A file a board was read from, kept with the job so it can be read again,
// replaced or added to later: what it is, where, the CAD tool it was read
// as, and a fingerprint of what it held then (so a file changed on disk
// since is noticed, never read again on its own).
struct JPSource {
    enum class Kind { Placements, Bom };

    Kind            kind = Kind::Placements;
    std::string     path;
    JPCadTool::Kind tool = JPCadTool::Kind::Other;
    std::string     fingerprint;

    // A fingerprint of a file's bytes now; empty when it cannot be read.
    static std::string fingerprintOf(const std::string& path);
};

} // inline namespace jf
