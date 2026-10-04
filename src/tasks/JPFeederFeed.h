// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPJobMachine.h"

#include "model/JPConfiguration.h"

#include <functional>
#include <string>

inline namespace jf {

// A feed, as OpenPnP's feeder.feed(): the feeder's count moved on (as its
// feed option says, JPFeeder::feed), then, for a strip with vision on, the
// holes it wants looked at found with the head's camera (as its parallax
// says, within half a hole pitch) and where its parts lie set from them. A
// hole not found, or found more than 2 mm from where it should be, is the
// strip's end: "Unable to locate reference hole. End of strip?", empty.
// Runs on a thread of its own; the model is touched only through `onMain`.
class JPFeederFeed {
public:
    using OnMain = std::function<void(const std::function<void()>&)>;

    static bool feed(JPConfiguration& config, const std::string& feederId, JPJobMachine& machine, const OnMain& onMain,
                     std::string& why, bool& empty);
};

} // inline namespace jf
