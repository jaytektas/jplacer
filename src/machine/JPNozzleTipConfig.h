// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPChangerStep.h"
#include "JPRunout.h"

#include <j/config/Json.h>

#include <map>
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
    // OpenPnP's Part Dimensions: the largest part it picks (diameter or
    // diagonal, tolerances in), and how far off a part may be picked
    // (bottom vision accepts a part no further off, and looks no further).
    double                     maxPartDiameterMm = 20;
    double                     maxPickToleranceMm = 1;
    // OpenPnP's Push and Drag Usage: whether it may push and drag (sturdy
    // enough for the side forces), and its outside diameter at its lowest 0.75 mm.
    bool                       pushAndDragAllowed = false;
    double                     diameterLowMm = 0;
    // Waited after a pick or place with this tip, on top of the nozzle's own.
    int                        pickDwellMs = 0;
    // OpenPnP's Place Blow-Off Level: the blow-off at place, when the part's package gives none (0: no blow-off).
    double                     placeBlowOffLevel = 0;
    int                        placeDwellMs = 0;
    // PART DETECTION by the vacuum, as in OpenPnP. After a pick (part on) and
    // after a place (part off), the vacuum level read is checked: by itself
    // ("Absolute": within low..high), or as its change from the level read
    // just before ("Difference": that level within low..high, its change
    // within diffLow..diffHigh). "None": not checked. Part off is read once
    // the valve has been opened for `probingMs` and closed for `dwellMs`.
    struct Sensing {
        std::string method = "None";
        double low = 0, high = 0, diffLow = 0, diffHigh = 0;
    };
    Sensing                    partOn, partOff;
    int                        partOffProbingMs = 0;
    int                        partOffDwellMs = 0;
    // RUNOUT: how its end swings as the nozzle turns, measured with the camera
    // looking up (JPRunout), one measuring for each nozzle it was on (by
    // nozzle id). Compensated in moves while `enabled`. Measured at
    // `divisions` angles round the circle, up to `misdetects` of them allowed
    // to fail, `zOffset` above the camera's focus, finding a round end
    // `visionDiameter` across (0: the tip's diameter).
    struct RunoutCalibration {
        bool   enabled = false;
        int    divisions = 6;
        int    misdetects = 0;
        double zOffset = 0;
        double visionDiameter = 0;
        static constexpr int kLeastDivisions = 3, kMostDivisions = 72;
    };
    RunoutCalibration                runoutCalibration;
    std::map<std::string, JPRunout>  runout;
    // The runout to compensate on nozzle `nozzleId`; null when none (or off).
    const JPRunout* runoutOn(const std::string& nozzleId) const {
        if (!runoutCalibration.enabled) return nullptr;
        const auto r = runout.find(nozzleId);
        return r == runout.end() ? nullptr : &r->second;
    }

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
