// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPPartsStore.h"

#include "job/JPPlacement.h"

#include <string>

inline namespace jf {

// A new part in the job made from what the files said about a placement, in
// the package its names (CAD footprint, then the supplier's package name)
// find: in the job, else copied from the library, else a new package of
// that name with no footprint yet. The library is not changed.
class JPPartMaker {
public:
    // The new part's id.
    static std::string fromPlacement(const JPPlacement& p, const JPPartsStore& library, JPPartsStore& job);
};

} // inline namespace jf
