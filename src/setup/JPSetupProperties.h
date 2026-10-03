// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "machine/JPCellConfig.h"

#include <j/core/JPropertyModel.h>

#include <string>
#include <vector>

inline namespace jf {

// What Machine Setup edits on each part of a cell: its settings as a
// property model, read from and written to the cell being set up, and how
// they are laid out, as OpenPnP lays out the same part: tabs, titled groups
// in each, and rows in each group (a label, then one or more settings side
// by side; X / Y / Z / Rotation as columns under a header; a place, with
// buttons to take it from where the machine is or go there). Only settings
// jplacer acts on are here; whatever else a cell file carries (brought from
// OpenPnP, for features not built yet) is kept as it is.
//
// A choice (a controller, an axis, a head) is shown and set by name.
class JPSetupProperties {
public:
    // A setting in a row: the property, and what is said before it ("" for
    // the first in a row, which the row's label names).
    struct Cell {
        std::string property;   // empty: an empty place, keeping the columns
        std::string label;
    };
    // What a place row's buttons use: the camera on the head, or the tool
    // chosen (a nozzle); and what an axis row takes, an axis's position.
    enum class Place { None, Location, Axis };
    struct Row {
        enum class Kind {
            Fields,    // label, settings
            Header,    // the column titles over the rows after it (cells' labels)
            Note,      // a line of text
            Actions,   // buttons (cells' labels; property: the action's name)
        };
        Kind        kind = Kind::Fields;
        std::string label;
        std::vector<Cell> cells;
        std::string text;                // a note
        Place       place = Place::None; // its cells are X, Y, Z, rotation (Location) or one axis (Axis)
        std::string axis;                // Place::Axis: which axis
    };
    struct Group {
        std::string      title;
        std::vector<Row> rows;
    };
    struct Tab {
        std::string        title;
        std::vector<Group> groups;
    };

    struct Form {
        std::string    title;       // what is being edited ("Axis x")
        JPropertyModel model;
        std::vector<Tab> tabs;
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
