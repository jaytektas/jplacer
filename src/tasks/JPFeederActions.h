// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPJobMachine.h"

#include "machine/JPVisionConfig.h"

#include "model/JPConfiguration.h"

#include <functional>
#include <string>
#include <utility>
#include <vector>

inline namespace jf {

// The machine's side of a feeder page's buttons (OpenPnP's wizard actions,
// run as machine tasks), on a thread of their own; the model touched only
// through `onMain`. An auto feeder's Test feed and Test post pick (its feed
// and post pick actuators with their values); a Schultz feeder's Test pre
// pick and Test post pick (its pre and post pick actuators with its feeder
// number, the feed count read after), Get ID, Get feed count, Get pitch and
// Get status (its actuators read with its feeder number), Clear feed count
// and Toggle pitch (actuated with it; the count cleared, the pitch read
// after); a slot Schultz feeder's Update location (its location's X and Y
// set from where its fiducial part is found near it, as OpenPnP's
// getHomeFiducialLocation); a Photon feeder's Find (its slot address asked),
// Feed and Feed 1mm (on the bus, the nozzle left where it is) and Search
// (every address asked, JPPhotonFeeders::findAll); a Bamboo feeder's Test
// feed and Test post pick, Preview Vision Features and Auto-Setup (JPBambooFeeder);
// a heap feeder's Clean DropBox and GetSamples (JPHeapFeeder, with the head's first nozzle). An action whose actuator (or fiducial part) is
// not set does nothing (the log says so).
class JPFeederActions {
public:
    using OnMain = std::function<void(const std::function<void()>&)>;
    struct Outcome {
        // What was read, for the page to show: (the reading's key, its value).
        std::vector<std::pair<std::string, std::string>> readings;
        // The feeder was changed (a slot Schultz feeder's location found by its fiducial).
        bool changed = false;
    };

    // `vision`: the machine's vision (its fiducial vision settings, for a fiducial part that names none).
    // `progress`: a Photon search's, each address as it is asked and answered (JPPhotonFeeders::SearchState).
    static bool run(JPConfiguration& config, const std::string& feederId, const std::string& action, JPJobMachine& machine,
                    const OnMain& onMain, const JPVisionConfig& vision, Outcome& outcome, std::string& why,
                    const std::function<void(int address, int state)>& progress = {});
};

} // inline namespace jf
