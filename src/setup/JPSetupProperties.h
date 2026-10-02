// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "machine/JPCellConfig.h"

#include <j/core/JPropertyModel.h>

#include <string>
#include <vector>

inline namespace jf {

// What Machine Setup edits on each part of a cell: its settings as a
// property model (grouped by category, in the order shown), read from and
// written to the cell being set up. Only settings jplacer acts on are here;
// whatever else a cell file carries (brought from OpenPnP, for features not
// built yet) is kept as it is.
//
// A choice (a controller, an axis, a head) is shown and set by name.
class JPSetupProperties {
public:
    struct Form {
        std::string    title;       // what is being edited ("Axis x")
        JPropertyModel model;
        // Properties whose change changes which others there are (an axis's
        // kind, a camera's head): the form is made again after one changes.
        std::vector<std::string> reshaping;
    };

    // The form for the node at `path` (JPSetupTree); an empty model for a
    // group, or a part not in `cell`. `profiles`: the firmware profiles a
    // controller can name. The model refers to `cell`, which must outlive it.
    static Form forNode(JPCellConfig& cell, const std::string& path, const std::vector<std::string>& profiles);
};

} // inline namespace jf
