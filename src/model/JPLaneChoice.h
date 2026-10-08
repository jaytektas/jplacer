// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPConfiguration.h"

#include <set>
#include <string>
#include <vector>

inline namespace jf {

// Where a part not on any feeder is loaded as a run reaches it (DESIGN.md, Load as you go): the lanes (strip
// feeders, laid by hand) that are free for it, best first: one holding nothing (or turned off), then one whose
// part the run no longer needs (said, so it is taken off first); of each, one of the part's tape width (its
// first packaging's) before one of another. A lane holding a part the run still needs is never offered.
class JPLaneChoice {
public:
    struct Lane {
        std::string feederId, name;
        std::string removePartId;   // what is taken off first; empty: nothing
        double      widthMm = 0;
        bool        widthFits = true;   // the part's tape width, or not known
    };
    // The lanes free for `partId`, best first; `stillNeeded`: the parts the run has yet to place.
    static std::vector<Lane> free(const JPConfiguration& config, const std::string& partId,
                                  const std::set<std::string>& stillNeeded);
    // What the part comes as, in words, for the prompt: "Cut tape, 8 mm Paper, part turned 90° in it".
    static std::string packagingWords(const JPConfiguration& config, const std::string& partId);
};

} // inline namespace jf
