// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPJobMachine.h"

#include "model/JPConfiguration.h"

#include <functional>
#include <string>

inline namespace jf {

// OpenPnP's BambooFeederAutoVision (and its AbstractPandaplacerVisionFeeder):
// a tape advanced by a feed actuator, the parts one feed brings picked in
// turn, its pick location kept true by finding its two sprocket holes with
// the head camera (JPFeederVision).
// The holes are looked at from their middle, up to three times until the
// farthest pick moves less than 0.3 mm (calibrate-max-passes,
// calibrate-tolerance-mm); as its Calibration Trigger says: once (OnFirstUse),
// on each tape feed until the precision wanted is reached (UntilConfident), on
// each tape feed (OnEachTapeFeed), or never (None).
// Runs on a thread of its own; the model is touched only through `onMain`.
class JPBambooFeeder {
public:
    using OnMain = std::function<void(const std::function<void()>&)>;

    // OpenPnP's feed: the nozzle over the pick location first when Move before
    // feed is set; on a normal feed with no part left of the last, calibrated,
    // the tape fed by its feed actuator (once for each feed pitch in a part
    // pitch), up to safe Z and calibrated again as its trigger says; the count on.
    static bool feed(JPConfiguration& config, const std::string& feederId, const std::string& nozzleId, JPJobMachine& machine,
                     const OnMain& onMain, std::string& why);
    // OpenPnP's assertCalibrated: holes at least 3 mm apart, and calibrated when not yet (or as the trigger says after a tape feed).
    static bool assertCalibrated(JPConfiguration& config, const std::string& feederId, JPJobMachine& machine, const OnMain& onMain,
                                 bool tapeFeed, std::string& why);
    // OpenPnP's performSprocketCalibration: the vision offset found again.
    static bool calibrate(JPConfiguration& config, const std::string& feederId, JPJobMachine& machine, const OnMain& onMain,
                          std::string& why);
    // OpenPnP's autoSetup, with the camera over the pick location (the one
    // nearest the reel, when a feed brings several): the pick location and
    // holes found from there, the statistics reset, then the holes calibrated;
    // the camera back over the pick location after. A trigger of None becomes UntilConfident.
    static bool autoSetup(JPConfiguration& config, const std::string& feederId, JPJobMachine& machine, const OnMain& onMain,
                          std::string& why);
    // OpenPnP's showFeatures (Preview Vision Features): the camera over the
    // pick location, the features found shown on it, nothing set.
    static bool showFeatures(JPConfiguration& config, const std::string& feederId, JPJobMachine& machine, const OnMain& onMain,
                             std::string& why);
    // OpenPnP's getJobPreparationLocation: where an uncalibrated feeder (with
    // a trigger) is visited before a job; none when it needs no visit.
    static std::optional<JPLocation> jobPreparationLocation(const JPFeeder& feeder);
    // OpenPnP's prepareForJob(true): calibrated when not yet.
    static bool prepareForJob(JPConfiguration& config, const std::string& feederId, JPJobMachine& machine, const OnMain& onMain,
                              std::string& why);
};

} // inline namespace jf
