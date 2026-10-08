// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <string>
#include <vector>

inline namespace jf {

// A manufacturer the library knows (DESIGN.md, Identifier): its name and the
// other names files give it ("Texas Instruments": "TI", "Texas Instruments
// Inc."), so an MPN's manufacturer is the same whichever name a BOM uses.
struct JPManufacturer {
    std::string              name;
    std::vector<std::string> akas;
};

} // inline namespace jf
