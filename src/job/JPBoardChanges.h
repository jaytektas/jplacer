// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPBoard.h"

#include <string>
#include <vector>

inline namespace jf {

// What a board read again changes, placement by placement (matched by
// designator), and the new board with what the person set on the old one
// kept where it still holds:
//
//  - moved or turned: kept (its part, a rotation set by hand, whether it is a
//    reference), except a reference's recorded position and look: it is
//    somewhere else now;
//  - a different part (value, MPN, supplier numbers or footprint changed):
//    its part is cleared, to be found again; a rotation set by hand and
//    whether it is a reference stay;
//  - removed: everything about it goes; new: nothing set.
class JPBoardChanges {
public:
    struct Change {
        enum class Kind { Added, Removed, Moved, Turned, DifferentPart };
        Kind        kind;
        std::string designator;
        std::string text;   // "moved 0.40 mm", "100nF → 1uF"
    };

    static std::vector<Change> compare(const JPBoard& before, const JPBoard& after);
    // `after`, with what was set on `before` carried over as above.
    static JPBoard merge(const JPBoard& before, JPBoard after);
    static const char* mark(Change::Kind k);   // "+", "−", "~"
};

} // inline namespace jf
