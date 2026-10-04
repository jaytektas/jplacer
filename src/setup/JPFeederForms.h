// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPSetupProperties.h"

#include "model/JPConfiguration.h"

#include <functional>
#include <string>

inline namespace jf {

// A feeder's tabs on the Feeders tab, as OpenPnP's feeder wizards lay them
// out (shown by JPSetupForm): for each kind its General Settings (Part,
// Feed and Pick Retry Count) and what the kind adds. A strip feeder has
// OpenPnP's Tape Settings (Part Pitch, Tape Width, Feed Count and Max Feed
// Count with their Reset and Auto Set buttons), Vision and Locations (the
// reference and next hole); a tray its Pick Location (the first part),
// Offsets, Tray Count and Feed Count; the other kinds their Pick Location.
// Each edit is made on the feeder at once, as everywhere in jplacer, so
// there is no Apply or Reset.
class JPFeederForms {
public:
    // `warn`: a value kept, but which will not work (a tray's offset of 0
    // with more than one part that way), to be said.
    static JPSetupProperties::Form forFeeder(JPConfiguration& config, const std::string& feederId,
                                             std::function<void(const std::string&)> warn);
    // A button of the form (its action's name) on the feeder; true when it changed it.
    static bool act(JPConfiguration& config, const std::string& feederId, const std::string& action);
};

} // inline namespace jf
