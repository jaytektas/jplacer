// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPJobMachine.h"

#include "machine/JPScripting.h"
#include "model/JPConfiguration.h"

#include <functional>
#include <string>

inline namespace jf {

// The Jog panel's Recycle (OpenPnP's recycleAction and Feeder.takeBackPart):
// the part on a nozzle put back into a feeder that holds it and can take it
// back (JPFeeder::canTakeBackPart), the nearest to the head's camera; at the
// place it was picked from, let go and checked gone (a heap: dropped back
// into the heap; a blinds feeder with its cover not open, fed again first),
// with the Feeder.BeforeTakeBack and Feeder.AfterTakeBack scripting events
// round it. Runs on a thread of its own; the model is touched only through `onMain`.
class JPFeederTakeBack {
public:
    using OnMain = std::function<void(const std::function<void()>&)>;

    // The feeder the part on `nozzleId` would go back to; empty when none can take it.
    static std::string feederFor(const JPConfiguration& config, const std::string& partId,
                                 const std::optional<JPLocation>& cameraAt);
    static bool takeBack(JPConfiguration& config, const std::string& nozzleId, JPJobMachine& machine, const OnMain& onMain,
                         JPScripting* scripting, std::string& why);
};

} // inline namespace jf
