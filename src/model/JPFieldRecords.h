// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPConfiguration.h"

#include <functional>
#include <string>
#include <vector>

inline namespace jf {

// Every thing of a kind there is now, with its values of the fields added (DESIGN.md, Fields): the
// library's parts, packages and their pads; the boards' and panels' (definitions and their uses) and their
// placements; the job's boards and panels where they lie and each panel's; the feeders; the vision
// settings; the job. What a field removed takes out, how many have a value counted, and a field's values
// made of another type, each thing changed marked to be saved (a board or panel dirty, the job dirty).
class JPFieldRecords {
public:
    // `f` given each thing's values; true back when it changed them.
    using Visit = std::function<bool(JPFieldValues&)>;
    // Whether any thing changed (each changed one marked to be saved).
    static bool each(JPConfiguration& config, JPJob* job, JPFieldSet::Of of, const Visit& f);

    // How many things have a value in `key`.
    static int count(JPConfiguration& config, JPJob* job, JPFieldSet::Of of, const std::string& key);
    // Every value in `key` taken out; how many there were.
    static int erase(JPConfiguration& config, JPJob* job, JPFieldSet::Of of, const std::string& key);
    // The values of `from` that `to` (the same field, of another type or choices) cannot read, as they read.
    static std::vector<std::string> unconvertible(JPConfiguration& config, JPJob* job, JPFieldSet::Of of,
                                                  const JPField& from, const JPField& to);
    // Each value of `from` made one of `to`'s (those it cannot read taken out).
    static void convert(JPConfiguration& config, JPJob* job, JPFieldSet::Of of, const JPField& from, const JPField& to);
};

} // inline namespace jf
