// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPPartsStore.h"

#include "job/JPPlacement.h"

#include <string>
#include <vector>

inline namespace jf {

// Whether a package's 0° is the CAD footprint's, so pin 1 lands on pad 1: a
// wrong 0° places every part of that footprint turned 90°, 180° or 270°.
// Each package is checked once, the cheapest way that settles it:
//
//  1. Cannot matter: two pads alike turned half round, on parts with no
//     polarity (resistors, non-polarised capacitors, inductors, ferrites, by
//     their designators R, C, L, FB): no check needed.
//  2. The file: where placements give pad 1's position (EasyEDA's Pad X/Y),
//     where the footprint's pad 1 lands at each placement's rotation is
//     compared with it. All agreeing: checked. All off by the same quarter
//     turn: the package's turn is that far out. Mixed: shown per placement.
//  3. Vision: a footprint that looks different at each quarter turn is
//     matched against the bare board at the four angles; a clear winner
//     settles it (done on the machine, JPBoardLocator's pattern matching).
//  4. The person: pads alike turned half round on a polarised part (SOIC,
//     QFP, diodes): vision cannot tell pin 1, so the person turns the
//     footprint drawn over the board until pad 1 is on the board's pin-1 mark.
//
// A package checked stays checked until its footprint or turn changes.
class JPRotationCheck {
public:
    enum class Way { CannotMatter, File, Vision, Person };

    // The way that can settle a package, from its footprint and the
    // placements using it (`placements`); the file only where they give pad 1.
    static Way way(const JPFootprint& f, const std::vector<const JPPlacement*>& placements);
    // The footprint looks the same turned by `quarters` quarter turns.
    static bool symmetric(const JPFootprint& f, int quarters);

    struct FileVerdict {
        int         compared = 0;        // placements with pad 1 given
        int         agreeing = 0;        // pad 1 where the package puts it
        int         quarters = 0;        // all off by this many quarter turns (anticlockwise), when `uniform`
        bool        uniform = false;     // every one compared off by the same (0 included)
        std::vector<std::string> odd;    // designators that disagree with the rest
    };
    // Pad 1 as the file gives it, against where the package's footprint puts
    // it at each placement's rotation (with `turnDeg`, the package's turn).
    static FileVerdict byFile(const JPFootprint& f, double turnDeg, const std::vector<const JPPlacement*>& placements);

    // The package is checked, for its footprint and turn as they are now.
    static bool checked(const JPPackage& k);
    static void markChecked(JPPackage& k, const char* by);
    static const char* name(Way w);
};

} // inline namespace jf
