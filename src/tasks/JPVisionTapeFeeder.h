// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPJobMachine.h"

#include "model/JPConfiguration.h"

#include <functional>
#include <string>

inline namespace jf {

// The feeders whose pick location is kept true by finding two of the tape's
// sprocket holes with the head camera (JPFeederVision): OpenPnP's
// BambooFeederAutoVision (its AbstractPandaplacerVisionFeeder), a tape
// advanced by a feed actuator, and ReferencePushPullFeeder, a tape advanced
// by a lever the head's feed actuator pushes and pulls; the parts one feed
// brings picked in turn.
// The holes are looked at from their middle, up to three times until the
// farthest pick moves less than 0.3 mm (calibrate-max-passes,
// calibrate-tolerance-mm); as its Calibration Trigger says: once (OnFirstUse),
// on each tape feed until the precision wanted is reached (UntilConfident), on
// each tape feed (OnEachTapeFeed), or never (None).
// Runs on a thread of its own; the model is touched only through `onMain`.
class JPVisionTapeFeeder {
public:
    using OnMain = std::function<void(const std::function<void()>&)>;

    // OpenPnP's feed. A Bamboo feeder: the nozzle over the pick location first
    // when Move before feed is set; on a normal feed with no part left of the
    // last, calibrated, the tape fed by its feed actuator (once for each feed
    // pitch in a part pitch), up to safe Z and calibrated again as its trigger
    // says; the count on. A push-pull feeder (feedPushPull).
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

private:
    // OpenPnP's ReferencePushPullFeeder.feed, on a normal feed with no part
    // left of the last: calibrated; its feed actuator over the start location
    // (moved by the vision offset where Vision Calibrate is ticked), then for
    // each actuation (the feed pitches in a part pitch, times the multiplier)
    // switched on, pushed through the mid locations ticked to the end, the
    // auxiliary actuator on, pulled back through those ticked, both off (the
    // first push and last pull complete, those between as Multi says), each at
    // its speed with its delay; up to safe Z, calibrated again as its trigger
    // says; the count on. A rotation axis is counted from where it is (Additive).
    static bool feedPushPull(JPConfiguration& config, const std::string& feederId, JPJobMachine& machine, const OnMain& onMain,
                             std::string& why);
};

} // inline namespace jf
