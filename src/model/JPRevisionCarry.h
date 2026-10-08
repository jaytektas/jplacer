// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPBoardRevision.h"

#include <string>
#include <vector>

inline namespace jf {

// The work a board's revision switched to was given from the one switched from (JPBoard::switchRevision):
// for placements the same in both (where the CAD puts them, how it turns them, their part), a rotation
// verified there, a verified mark, and a part chosen there. `before` is the revision as it was, to undo it.
class JPRevisionCarry {
public:
    std::string              from;        // the revision's label it came from
    std::vector<std::string> rotations;   // placements given a verified rotation
    std::vector<std::string> verified;    // placements given only the verified mark (the rotation was the same)
    std::vector<std::string> parts;       // placements whose part was chosen as there
    JPBoardRevision          before;

    bool empty() const { return rotations.empty() && verified.empty() && parts.empty(); }
};

} // inline namespace jf
