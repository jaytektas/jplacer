// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPPartsStore.h"

#include "job/JPPlacement.h"

inline namespace jf {

// The angle a placement is placed at, and where it comes from: the imported
// rotation plus its package's turn (the turn from the CAD footprint's 0° to
// the package's), unless an angle was set on the placement by hand.
//
//   AsImported: what the CAD files said (its package adds no turn);
//   AsPart:     the imported rotation plus its package's turn;
//   Unique:     set on this placement alone, agreeing with neither.
struct JPPlacementRotation {
    enum class Source { AsImported, AsPart, Unique };

    double degrees = 0;   // 0 <= degrees < 360
    Source source = Source::AsImported;

    static JPPlacementRotation of(const JPPlacement& p, const JPPartsStore& job);
    static const char* name(Source s);
    // An angle brought into 0..360.
    static double normal(double degrees);
};

} // inline namespace jf
