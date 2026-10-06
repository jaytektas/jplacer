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
//     Nozzle Tips         a nozzle tip each, and under it
//       Load, Unload      its changer's steps
//     Cameras             fixed to the machine (looking up at the nozzles)
//     Actuators           on the machine, not a head
//     Signalers           a signaler each
//
// Each node has a path naming it: "machine", "driver:<id>", "axis:<id>",
// "head:<id>", "nozzle:<id>", "nozzletip:<id>", "camera:<id>",
// "actuator:<id>", "signaler:<id>", "step:<tipId>:<load|unload>:<index>", and for a group
// "group:<what>" ("group:drivers", "group:axes", "group:heads",
// "group:nozzletips", "group:signalers") or, for a group that belongs to a head (or to the
// machine, an empty head) or a tip, "group:<what>:<owner>"
// ("group:nozzles:H1", "group:cameras:", "group:load:T1").
//
// A tip that unloads by running its loading backwards shows those steps
// under Unload, to see; they are changed by changing loading.
class JPSetupTree {
public:
    struct Node {
        std::string       label;
        std::string       path;
        std::vector<Node> children;
        std::string       icon;   // OpenPnP's icon for its kind (JPOpenPnpIcons' name); empty for none
    };

    static Node build(const JPCellConfig& cell);

    // A path's parts: its kind ("axis", "group", "machine"), the id after it
    // (a group's: what it holds; a step's: its index), and whose it is (a
    // group's head, "" for the machine's, or tip; a step's tip), and a
    // step's list ("load", "unload").
    struct Path {
        std::string kind, id, owner, list;
    };
    static Path parse(const std::string& path);
    // The path of the group an item is in ("axis:X" -> "group:axes"); a group
    // is its own. Empty for the machine.
    static std::string groupOf(const JPCellConfig& cell, const std::string& path);
    // The labels from the top down to the node at `path` (for the tree view's
    // selection); empty when there is no such node.
    static std::vector<std::string> labelsTo(const Node& root, const std::string& path);
    // A changer step in words, numbered from 1 ("2. Move to X 4, Z -27 at 50%").
    static std::string stepLabel(const JPCellConfig& cell, const JPChangerStep& step, size_t index);
    // A number as short as it can be put (-16, 0.5, 410.318).
    static std::string shortNumber(double v);
};

} // inline namespace jf
