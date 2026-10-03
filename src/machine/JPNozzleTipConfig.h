// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPChangerStep.h"

#include <j/config/Json.h>

#include <string>
#include <vector>

inline namespace jf {

// A nozzle tip: one of the tips the machine's nozzles take, the diameter of
// its end as the camera looking up sees it (in mm; 0 when not known), by
// which the camera finds it, and how it is loaded onto a nozzle and unloaded
// from one. Which nozzles it fits, and which it is on, the nozzles say
// (JPNozzleConfig).
//
// Loading runs `loadSteps`, taught for this tip on this machine; none: it is
// put on by hand. The first move is approached from safe Z (up, across, then
// down to it), and loading ends at safe Z. Unloading is loading run
// backwards (unloadingSteps), unless the tip has steps of its own for it.
struct JPNozzleTipConfig {
    std::string                id;
    std::string                name;
    double                     diameter = 0;
    std::vector<JPChangerStep> loadSteps;
    bool                       unloadReversesLoad = true;
    std::vector<JPChangerStep> unloadSteps;   // when it does not

    // The steps that unload it: its own, or loading backwards. Backwards,
    // each move goes to the place the one before it went to, at the speed
    // of the move it undoes, starting from the last place loading reached;
    // an actuator is switched the other way; a move away from safe Z is
    // undone by going up, then across to where that move started.
    std::vector<JPChangerStep> unloadingSteps() const;
    static std::vector<JPChangerStep> reversed(const std::vector<JPChangerStep>& steps);

    // What is wrong with its steps (a list that does not start with a move
    // giving X, Y and Z), in words; empty when sound.
    std::vector<std::string> problems() const;

    static JPNozzleTipConfig fromJson(const JJson& j);
    JJson toJson() const;
};

} // inline namespace jf
