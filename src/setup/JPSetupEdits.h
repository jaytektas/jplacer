// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "machine/JPCellConfig.h"

#include <string>

inline namespace jf {

// What Machine Setup's Add, Remove and the arrows do to a cell being set up,
// by the paths JPSetupTree names its nodes with.
class JPSetupEdits {
public:
    // What Add would add at `path` (the group selected, or the group of the
    // part selected): "Axis", "Nozzle"… Empty when nothing is added there.
    static std::string addable(const JPCellConfig& cell, const std::string& path);
    // Add a new part there, with a new id and a name saying what it is; it
    // goes on the group's head. The new part's path, or empty.
    static std::string add(JPCellConfig& cell, const std::string& path);
    // Remove the part at `path`. Refused (false, and why) while anything else
    // names it: an axis a nozzle moves on, a controller an axis is on, the
    // light a camera switches, a head with parts on it, a nozzle tip on a
    // nozzle. A nozzle tip goes from the lists of the nozzles it fits.
    static bool remove(JPCellConfig& cell, const std::string& path, std::string& why);
    // Move the part at `path` up (-1) or down (+1) among the others in its
    // group. False when it is already at that end.
    static bool move(JPCellConfig& cell, const std::string& path, int by);

private:
    static std::string newId(const JPCellConfig& cell, const std::string& prefix);
};

} // inline namespace jf
