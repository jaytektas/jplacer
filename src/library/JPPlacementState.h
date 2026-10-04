// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPPartsStore.h"

#include "job/JPPlacement.h"

#include <string>

inline namespace jf {

// Whether a placement can be placed, as far as its parts go: placement ->
// part -> package -> footprint, each link to one, all there. The first
// missing link is named. A conflict: its part's package is not the one its
// footprint name (or supplier package name) belongs to, or its footprint has
// a different number of pads from the pins the file counts. A guess: given
// by value alone, to be confirmed. (Tips and feeders are the machine's, and
// checked where the job is run.)
struct JPPlacementState {
    enum class Kind { Fiducial, DoNotPlace, NoPart, NoPackage, NoFootprint, Conflict, Guess, Ready };

    Kind        kind = Kind::Ready;
    std::string why;   // in words, for the row

    bool placeable() const { return kind == Kind::Ready; }
    static JPPlacementState of(const JPPlacement& p, const JPPartsStore& job);
    // What a kind is called, as the row shows it.
    static const char* name(Kind kind);
};

} // inline namespace jf
