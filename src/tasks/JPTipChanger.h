// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "machine/JPCell.h"

#include <functional>
#include <string>
#include <vector>

inline namespace jf {

// Runs a nozzle tip's load or unload steps (JPChangerStep) with a nozzle, on a
// thread of its own (it waits on each move).
//
// A Move's place is where the nozzle goes, in the axes' own coordinates (the
// nozzle's offset on the head taken off; not squared). The first move of the
// list comes in from safe Z: up, across (and turned), then down to its Z.
// The others go straight there. The list ends with the head at safe Z.
// Every move goes at its step's speed times the machine's speed.
//
// A wrong move is a crash, so every step can be asked about first (`ask`
// returning false stops before it), and an Ask step always is. Stopped or
// failed, what was done is left as it is: the person looks, and says which
// tip is on the nozzle.
class JPTipChanger {
public:
    struct Hooks {
        // A question for the person (the step about to run, or an Ask step's
        // message): true to carry on. Called on the changer's thread; waits
        // for the answer.
        std::function<bool(const std::string& question)> ask;
        // What is being done now, for the person to follow.
        std::function<void(const std::string& step)> progress;
    };

    // Run `steps` with `nozzle`: `what` names them in questions ("Loading
    // 505L"); `names`, the cell's settings as they were when the change was
    // asked for, names their actuators. `everyStep`: ask before each step.
    // False with `why` when a step failed or was stopped.
    static bool run(JPCell& cell, const JPCellConfig& names, const JPNozzleConfig& nozzle,
                    const std::vector<JPChangerStep>& steps, const std::string& what, bool everyStep,
                    const Hooks& hooks, std::string& why);

    // A step in words, with the nozzle's coordinates: "move to X 410.278
    // Y 190 Z -27 at 50%".
    static std::string describe(const JPCellConfig& cell, const JPChangerStep& step);
};

} // inline namespace jf
