// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPPartsStore.h"

inline namespace jf {

// What a new library starts with: the common packages and their footprints,
// from their standards (IPC-7351 land patterns, nominal; JEDEC outlines),
// each known by the names KiCad, EasyEDA and suppliers usually give it, so
// most boards find their packages without drawing any. No parts: those come
// from jobs.
//
// Chip sizes 01005 to 2512; SOT-23, -5, -6; SOT-223; SOD-123, SOD-323;
// SMA, SMB, SMC; SOIC (narrow and wide); TSSOP; QFN; LQFP.
class JPStarterLibrary {
public:
    static void fill(JPPartsStore& store);
};

} // inline namespace jf
