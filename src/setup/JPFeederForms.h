// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPSetupProperties.h"

#include "model/JPConfiguration.h"

#include <functional>
#include <map>
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
        // A Photon search's progress: each address's state (JPSearchStrip's); empty: not searching.
        std::function<std::vector<int>()> searchStates;
        // What a button last read from the machine (a Schultz feeder's ID,
        // feed count, pitch, status), by its action; empty: nothing.
        std::function<std::string(const std::string& action)> reading;
        // A strip feeder's Auto Setup under way: its button then says Cancel Auto Setup.
        bool autoSetupRunning = false;
        // The fonts OCR can read (a push-pull feeder's OCR Font Name choices, as OpenPnP lists them).
        std::vector<std::string> fonts;
        // A push-pull feeder's Setup OCR Region under way: what its going-on button says (empty: not under way).
        std::string ocrRegionStep;
        // A push-pull feeder's Clone … Settings? ticks, the page's own (by "location", "tape", "vision", "pushPull").
        std::map<std::string, bool>* cloneChoices = nullptr;
    };
    // `warn`: a value kept, but which will not work (a tray's offset of 0
    // with more than one part that way), to be said.
    static JPSetupProperties::Form forFeeder(JPConfiguration& config, const std::string& feederId,
                                             std::function<void(const std::string&)> warn, const Options& options);
    // Whether a button of the form is done on the machine (JPFeederActions),
    // not on the feeder.
    static bool isMachineAction(const std::string& action);
    // The buttons of a feeder's page that read from the machine, to be read
    // when it is shown with the machine on (as OpenPnP's Schultz wizard does).
    static std::vector<std::string> readsOnShow(const JPFeeder& feeder);
    // A button of the form (its action's name) on the feeder; true when it
    // changed it; false with `why` when it could not (empty: nothing to do).
    // `reading`: what the page's buttons last read (a slot Schultz feeder's Load takes its Get ID).
    static bool act(JPConfiguration& config, const std::string& feederId, const std::string& action, std::string& why,
                    const std::function<std::string(const std::string&)>& reading = {});
};

} // inline namespace jf
