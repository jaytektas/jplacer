// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPJobMachine.h"

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
// after). An action whose actuator is not set does nothing (the log says so).
class JPFeederActions {
public:
    using OnMain = std::function<void(const std::function<void()>&)>;
    // What was read, for the page to show: (the reading's key, its value).
    using Readings = std::vector<std::pair<std::string, std::string>>;

    static bool run(JPConfiguration& config, const std::string& feederId, const std::string& action, JPJobMachine& machine,
                    const OnMain& onMain, Readings& readings, std::string& why);
};

} // inline namespace jf
