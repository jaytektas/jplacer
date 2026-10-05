// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPFeeder.h"
#include "JPLength.h"
#include "JPLocation.h"

#include <optional>

inline namespace jf {

// The geometry of OpenPnP's FeederVisionHelper, for the feeders that keep two
// sprocket holes and a pick location (BambooFeederAutoVision,
// ReferencePushPullFeeder): the tape's own frame, EIA-481's (sprocket holes on
// top, advancing to +X, the pick location at its origin), its angle on the
// machine taken from hole 1 to hole 2; the parts one feed brings, and where
// each is picked, moved by what vision found (the vision offset).
class JPFeederTape {
public:
    struct Params {
        JPLength   partPitch { 4, JPLengthUnit::Millimeters };
        JPLength   feedPitch { 4, JPLengthUnit::Millimeters };
        long       feedMultiplier = 1;
        JPLocation partLocation { JPLengthUnit::Millimeters };
        JPLocation hole1Location { JPLengthUnit::Millimeters };
        JPLocation hole2Location { JPLengthUnit::Millimeters };
    };
    // A feeder's, as OpenPnP keeps them (part-pitch, feed-pitch, location, hole-1-location, hole-2-location).
    static Params of(const JPFeeder& feeder);

    // The parts one feed brings (a feed of a whole number of feed pitches, at least a part pitch, times the multiplier).
    static long partsPerFeedCycle(const Params& params);
    // The tape's frame on the machine: the pick location turned the way the holes run, less the vision offset.
    static JPLocation transform(const std::optional<JPLocation>& visionOffset, const Params& params);
    static JPLocation feederToMachine(const JPLocation& feederLocation, const std::optional<JPLocation>& visionOffset,
                                      const Params& params);
    static JPLocation machineToFeeder(const JPLocation& machineLocation, const std::optional<JPLocation>& visionOffset,
                                      const Params& params);
    // Where part `partInCycle` (1-based, of partsPerFeedCycle) is picked: the last at the pick location, the
    // others a part pitch further back along the tape each; turned by `rotationInFeeder` from the tape.
    static JPLocation partLocation(long partInCycle, const std::optional<JPLocation>& visionOffset, const Params& params,
                                   double rotationInFeeder);
    // The calibration statistics such a feeder keeps (calibration-count,
    // sum-of-errors, sum-of-error-squares): how far each calibration moved the
    // farthest pick from the last, on average and as a 95% confidence limit
    // (assuming a normal distribution); sufficient once calibrated at least
    // calibrate-min-statistic times with the limit within precision-wanted.
    static JPLength precisionAverage(const JPFeeder& feeder);
    static JPLength precisionConfidenceLimit(const JPFeeder& feeder);
    static bool     precisionSufficient(const JPFeeder& feeder);
    static void     addCalibrationError(JPFeeder& feeder, const JPLength& error);
    // OpenPnP's resetCalibrationStatistics: none, and not calibrated.
    static void     resetCalibrationStatistics(JPFeeder& feeder);
    // OpenPnP's Discard Parts: the count rounded up to the end of the feed cycle, the calibration forgotten.
    static void discardParts(JPFeeder& feeder);
    // OpenPnP's Location.unitVectorTo: from `a` towards `b`, of length 1 (not finite when they are the same).
    static JPLocation unitVector(const JPLocation& a, const JPLocation& b);
    // OpenPnP's Location.getLinearDistanceToLineSegment: `p` from the segment a-b (in p's units).
    static double distanceToSegment(const JPLocation& p, const JPLocation& a, const JPLocation& b);
};

} // inline namespace jf
