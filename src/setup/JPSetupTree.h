// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "machine/JPCellConfig.h"

#include <string>
#include <vector>

inline namespace jf {

// A cell as Machine Setup shows it: a tree of what the machine is made of.
//
//   Machine
//     Controllers         a controller each
//     Axes                an axis each
//     Heads               a head each, and on it
//       Nozzles, Cameras, Actuators
//     Cameras             fixed to the machine (looking up at the nozzles)
//     Actuators           on the machine, not a head
//
// Each node has a path naming it: "machine", "driver:<id>", "axis:<id>",
// "head:<id>", "nozzle:<id>", "camera:<id>", "actuator:<id>", and for a group
// "group:<what>" ("group:drivers", "group:axes", "group:heads") or, for a
// group that belongs to a head (or to the machine, an empty head),
// "group:<what>:<headId>" ("group:nozzles:H1", "group:cameras:").
class JPSetupTree {
public:
    struct Node {
        std::string       label;
        std::string       path;
        std::vector<Node> children;
    };

    static Node build(const JPCellConfig& cell);

    // A path's parts: its kind ("axis", "group", "machine"), the id after it
    // (a group's: what it holds), and a group's head ("" for the machine's).
    struct Path {
        std::string kind, id, headId;
    };
    static Path parse(const std::string& path);
    // The path of the group an item is in ("axis:X" -> "group:axes"); a group
    // is its own. Empty for the machine.
    static std::string groupOf(const JPCellConfig& cell, const std::string& path);
    // The labels from the top down to the node at `path` (for the tree view's
    // selection); empty when there is no such node.
    static std::vector<std::string> labelsTo(const Node& root, const std::string& path);
};

} // inline namespace jf
