// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPPartsStore.h"

#include "job/JPPlacement.h"

#include <string>
#include <vector>

inline namespace jf {

// Gives each placement without a part its part in the job, from what the
// files said about it, strongest first:
//
//  1. a supplier number the job or the library knows: that part, certain;
//  2. else its MPN (compared as JPPartsStore::mpnKey): certain too; the
//     manufacturer is not matched on (it is written many ways);
//  3. else a library part with the same value (JPValue::same), every rating
//     both give equal, in the package its footprint name finds: a guess,
//     marked for the person to confirm;
//  4. else a new part in the job, from the placement's fields, in the
//     package its names find (in the job, else copied from the library),
//     else a new package of that name with no footprint yet.
//
// Placements that are the same thing (by supplier number, else MPN, else
// value, ratings and footprint name) share one part. A library part is
// copied into the job (JPPartsStore::copyPart); the library is not changed.
// Fiducials have no part. Placements that already have one keep it.
class JPPartMatcher {
public:
    struct Result {
        int certain = 0;   // placements matched by number or MPN
        int guessed = 0;   // by value, ratings and package, to confirm
        int created = 0;   // parts new to the job
        int noPart  = 0;   // placements the files said nothing about
    };

    static Result match(std::vector<JPPlacement>& placements, const JPPartsStore& library, JPPartsStore& job);
};

} // inline namespace jf
