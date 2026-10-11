// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPConfiguration.h"
#include "JPFeeder.h"
#include "JPPart.h"

#include <string>
#include <vector>

inline namespace jf {

// What a feeder takes from the packaging its part comes in (the part's
// Packagings, on its Library tab): the part's rotation in it, the tape's
// width and the pitch between pockets, each into the feeder's own setting of
// it where its kind has one (a strip feeder's Rotation In Tape, Tape Width
// and Part Pitch; a push-pull or Bamboo feeder's Rotation in Tape and Part
// Pitch; a blinds feeder's Rotation in Tape and Pocket Pitch; a Neoden4's
// Rotation In Tape and Pitch In Tape; a drag or lever feeder's Part Pitch; a
// Photon's or Rapid's pitch). Loading a part takes the one packaging it comes
// in, else the one last chosen for it; with several and none chosen yet, the
// user chooses (the feeder's Packaging).
class JPFeederPackaging {
public:
    // Whether a feeder of its kind takes anything from a packaging.
    static bool takes(const JPFeeder& feeder);
    // Each of the part's packagings as the feeder's Packaging lists them ("1: Cut tape, 8 mm, 4 mm pitch, -90°").
    static std::vector<std::string> labels(const JPPart& part);
    // The packaging whose values the feeder has now (of those its kind takes), or -1: its own.
    static int matching(const JPFeeder& feeder, const JPPart& part);
    // The packaging's values put into the feeder; what was put, in words ("rotation -90°, 8 mm tape, 4 mm
    // pitch"; empty: nothing its kind takes).
    static std::string apply(JPFeeder& feeder, const JPPart::Packaging& packaging);
    // The packaging loading the part takes: its only one, else the one last chosen for it; -1: the user's to choose.
    static int forLoading(const JPPart& part);
    // The part `partId` loaded onto the feeder: set, and its packaging's values taken (forLoading) when it has
    // one to take; what was taken, as apply says it.
    static std::string load(JPConfiguration& config, JPFeeder& feeder, const std::string& partId);
    // Packaging `index` of the feeder's part chosen for it: its values taken, and kept as the part's last chosen.
    static std::string choose(JPConfiguration& config, JPFeeder& feeder, int index);
};

} // inline namespace jf
