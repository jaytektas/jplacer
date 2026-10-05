// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPJobMachine.h"

#include "model/JPConfiguration.h"

#include <functional>
#include <string>

inline namespace jf {

// OpenPnP's Rapid feeder Scan: the head camera from the Scan Start to the
// Scan End Location, a Scan Increment at a time (the end included), reading
// the QR codes it sees (a code seen again keeps where it was first seen).
// Each code is a Rapid feeder: the one named by it (else a new one, named
// so, given the first part when it has none), its location where the code is
// (its Z and rotation kept) and its address the code.
// Runs on a thread of its own; the model is touched only through `onMain`.
class JPRapidScan {
public:
    using OnMain = std::function<void(const std::function<void()>&)>;
    // `feederId`: the feeder whose scan settings are used. The codes found, by count, in `found`.
    static bool scan(JPConfiguration& config, const std::string& feederId, JPJobMachine& machine, const OnMain& onMain, int& found,
                     std::string& why);
};

} // inline namespace jf
