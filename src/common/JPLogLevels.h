// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <j/core/Log.h>

#include <map>
#include <string>
#include <vector>

inline namespace jf {

// How much the log says: one level for every category, and any category
// given one of its own (JLog). Read from the log and put into it, and kept as
// text ("info; machine.cell=debug; camera.frames=off").
struct JPLogLevels {
    JLogLevel                         global = JLogLevel::Info;
    std::map<std::string, JLogLevel>  own;

    // As the log has them now, for every category jplacer has and any used since.
    static JPLogLevels current();
    // Into the log: the global level, each own level, and every other category back to the global.
    void apply() const;

    std::string toText() const;
    static JPLogLevels fromText(const std::string& text);

    // The levels a person chooses from, in order, and what they are called.
    static const std::vector<JLogLevel>& choices();
    static std::string name(JLogLevel level);
    // Every category: jplacer's own and any used since (a library's), sorted.
    static std::vector<std::string> categories();
};

} // inline namespace jf
