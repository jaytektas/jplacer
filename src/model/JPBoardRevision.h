// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPBoardPart.h"
#include "JPPlacement.h"

#include <j/config/Json.h>

#include <string>
#include <vector>

inline namespace jf {

// One revision of a board (DESIGN.md, Board revisions): its label (rev A, rev B…), when it was made, how it
// came from the one before, and, complete, the files it was imported from, its placements and its parts, so
// it can be opened, compared or built as it was left. The board shown is one of them (JPBoard::revision()).
class JPBoardRevision {
public:
    std::string              label;
    std::string              made;      // JPWhen::now(); empty: before revisions were kept
    std::string              summary;   // the upgrade that made it, in words ("182 unchanged, 4 moved…"); empty: the first
    std::vector<JJson>       provenance;
    std::vector<JPPlacement> placements;
    std::vector<JPBoardPart> parts;

    // Its label, when it was made and its summary; `content`: its files, placements and parts too.
    JJson toJson(bool content) const;
    static JPBoardRevision fromJson(const JJson& j);
    // The label after `label`: its last number, or a last letter standing alone, counted on ("rev A" to
    // "rev B", "v9" to "v10"); `label` and " 2" when it ends in neither ("first" to "first 2").
    static std::string next(const std::string& label);
};

} // inline namespace jf
