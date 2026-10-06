// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "machine/JPCell.h"
#include "model/JPFootprint.h"
#include "tasks/JPSimulatedPnpCheck.h"

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>

inline namespace jf {

// Simulation Mode's Pick & Place Checking for the cell (JPCell::PnpCheck):
// the footprint of the part each nozzle is given (kept as it is given, on
// the main thread), checked against the head's image camera's picture as
// OpenPnP's ImageCamera does (JPSimulatedPnpCheck), on the cell's thread,
// with the camera's tolerances (OpenPnP's defaults when it has none).
class JPlacerPnpChecking {
public:
    // The part on `nozzleId`, by its package's footprint (none: no part, or none known), and its height (mm; 0 not known).
    void hold(const std::string& nozzleId, std::shared_ptr<const JPFootprint> footprint, double heightMm);
    // What the cell calls.
    JPCell::PnpChecker checker();
    // The part on a nozzle (no footprint: no part), from any thread.
    struct Held {
        std::shared_ptr<const JPFootprint> footprint;
        double                             heightMm = 0;
    };
    using Holder = std::function<Held(const std::string& nozzleId)>;
    Holder holder() const;

private:
    struct State {
        std::mutex                                                   mutex;
        std::map<std::string, std::shared_ptr<const JPFootprint>>   footprints;   // by nozzle id
        std::map<std::string, double>                               heights;      // by nozzle id
        JPSimulatedPnpCheck                                          engine;
    };
    std::shared_ptr<State> m_state = std::make_shared<State>();
};

} // inline namespace jf
