// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPSetupProperties.h"

#include "model/JPConfiguration.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

inline namespace jf {

// A feeder's tabs on the Feeders tab, as OpenPnP's feeder wizards lay them
// out (shown by JPSetupForm): for each kind its General Settings (Part,
// Feed and Pick Retry Count) and what the kind adds. A strip feeder has
// OpenPnP's Tape Settings (Part Pitch, Tape Width, Feed Count and Max Feed
// Count with their Reset and Auto Set buttons), Vision and Locations (the
// reference and next hole); a tray its Pick Location (the first part),
// Offsets, Tray Count and Feed Count; a drag feeder its pin's actuators,
// feed start and end, and the template its vision looks for; the other
// kinds their Pick Location.
// Each edit is made on the feeder at once, as everywhere in jplacer, so
// there is no Apply or Reset.
class JPFeederForms {
public:
    // What a form shows besides the feeder.
    struct Options {
        // The machine's actuators by name (an auto feeder's choices).
        std::vector<std::string> actuators;
        // A drag feeder's selection on the camera under way: its Select
        // button then says Confirm, and its Cancel can be pressed.
        enum class Selecting { None, Template, AreaOfInterest };
        Selecting selecting = Selecting::None;
        // A drag feeder's template image (null: none).
        std::function<std::shared_ptr<const JPFrame>()> templateImage;
    };
    // `warn`: a value kept, but which will not work (a tray's offset of 0
    // with more than one part that way), to be said.
    static JPSetupProperties::Form forFeeder(JPConfiguration& config, const std::string& feederId,
                                             std::function<void(const std::string&)> warn, const Options& options);
    // A button of the form (its action's name) on the feeder; true when it
    // changed it; false with `why` when it could not (empty: nothing to do).
    static bool act(JPConfiguration& config, const std::string& feederId, const std::string& action, std::string& why);
};

} // inline namespace jf
