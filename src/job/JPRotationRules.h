// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

inline namespace jf {

// The rotation check as a rule in the job's checklist (JPRotationCheck): on
// or off, and which of its ways it may use. A way turned off is passed over,
// and what it would have settled falls to the next. Each job keeps its own;
// Preferences holds what a new job starts with.
struct JPRotationRules {
    bool check = true;              // the rule itself
    bool skipCannotMatter = true;   // no check where the rotation cannot matter
    bool byFile = true;             // the file's pad 1 (nothing moves)
    bool byVision = true;           // the camera's four angles
    bool byPerson = true;           // the person, on the board
};

} // inline namespace jf
