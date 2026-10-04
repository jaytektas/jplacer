// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPJobMachine.h"

#include "model/JPConfiguration.h"

#include <functional>
#include <string>

inline namespace jf {

// A feed, as OpenPnP's feeder.feed(): the feeder's count moved on (as its
// feed option says, JPFeeder::feed); for an auto feeder its feed actuator
// actuated (the nozzle taken over the pick place first when it says so);
// then, for a strip with vision on, the
// holes it wants looked at found with the head's camera (as its parallax
// says, within half a hole pitch) and where its parts lie set from them. A
// hole not found, or found more than 2 mm from where it should be, is the
// strip's end: "Unable to locate reference hole. End of strip?", empty.
// Runs on a thread of its own; the model is touched only through `onMain`.
class JPFeederFeed {
public:
    using OnMain = std::function<void(const std::function<void()>&)>;

    // `nozzleId`: the nozzle that will pick (an auto feeder moves it over its pick place first when it says so).
    static bool feed(JPConfiguration& config, const std::string& feederId, const std::string& nozzleId, JPJobMachine& machine,
                     const OnMain& onMain, std::string& why, bool& empty);
    // OpenPnP's postPick: an auto feeder's post-pick actuator actuated.
    static bool postPick(JPConfiguration& config, const std::string& feederId, JPJobMachine& machine, const OnMain& onMain,
                         std::string& why);
};

} // inline namespace jf
