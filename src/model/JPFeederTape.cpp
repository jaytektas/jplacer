// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPFeederTape.h"

#include <cmath>

inline namespace jf {

namespace {
constexpr JPLengthUnit kMm = JPLengthUnit::Millimeters;
// A normal distribution's 95% factor.
constexpr double kConfidence95 = 1.64;

// Its vision tape's own (another kind: a new one's).
const JPVisionTape& tapeOf(const JPFeeder& f) {
    static const JPPushPullFeeder none;
    const JPVisionTape* t = f.visionTape();
    return t ? *t : none;
}
}

JPFeederTape::Params JPFeederTape::of(const JPFeeder& feeder) {
    Params p;
    const JPVisionTape& t = tapeOf(feeder);
    p.partPitch = t.partPitch;
    p.feedPitch = t.feedPitch;
    const JPPushPullFeeder* pushPull = feeder.as<JPPushPullFeeder>();
    p.feedMultiplier = std::max(1, pushPull ? pushPull->feedMultiplier : 1);
    p.partLocation = feeder.location();
    p.hole1Location = t.hole1;
    p.hole2Location = t.hole2;
    return p;
}

long JPFeederTape::partsPerFeedCycle(const Params& params) {
    const double part = params.partPitch.convertToUnits(kMm).value(), feed = params.feedPitch.convertToUnits(kMm).value();
    if (part <= 0 || feed <= 0) return 1;
    const long feedsPerPart = long(std::ceil(part / feed));
    return std::max(1L, long(std::lround(double(params.feedMultiplier) * std::ceil(double(feedsPerPart) * feed / part))));
}

JPLength JPFeederTape::precisionAverage(const JPFeeder& feeder) {
    const JPVisionTape& t = tapeOf(feeder);
    if (t.calibrationCount <= 0) return JPLength(0, kMm);
    return JPLength(t.sumOfErrorsMm / t.calibrationCount, kMm);
}

JPLength JPFeederTape::precisionConfidenceLimit(const JPFeeder& feeder) {
    const JPVisionTape& t = tapeOf(feeder);
    const int count = t.calibrationCount;
    if (count < 2) return JPLength(0, kMm);
    // The errors are distances about the true holes' centre: zero is their mean.
    const double variance = t.sumOfErrorSquaresMm / (count - 1);
    const double scatter = std::sqrt(variance / std::sqrt(double(count)));
    // 95% confidence, normal distribution.
    return JPLength(scatter * kConfidence95, kMm);
}

bool JPFeederTape::precisionSufficient(const JPFeeder& feeder) {
    const JPVisionTape& t = tapeOf(feeder);
    if (t.calibrationCount < t.calibrateMinStatistic) return false;
    const double wanted = t.precisionWantedMm;
    return precisionConfidenceLimit(feeder).convertToUnits(kMm).value() / wanted <= 1.0;
}

void JPFeederTape::addCalibrationError(JPFeeder& feeder, const JPLength& error) {
    JPVisionTape* t = feeder.visionTape();
    if (!t) return;
    const double e = error.convertToUnits(kMm).value();
    t->sumOfErrorsMm += e;
    // mm squared, really: its square root is what is used.
    t->sumOfErrorSquaresMm += e * e;
    ++t->calibrationCount;
}

void JPFeederTape::resetCalibrationStatistics(JPFeeder& feeder) {
    if (JPVisionTape* t = feeder.visionTape()) {
        t->sumOfErrorsMm = t->sumOfErrorSquaresMm = 0;
        t->calibrationCount = 0;
    }
    feeder.visionOffset.reset();
}

void JPFeederTape::discardParts(JPFeeder& feeder) {
    Params tape = of(feeder);
    tape.feedMultiplier = 1;
    const long cycle = partsPerFeedCycle(tape);
    const long count = feeder.feedCount();
    feeder.setFeedCount(int(((count - 1) / cycle + 1) * cycle));
    feeder.visionOffset.reset();
}

JPLocation JPFeederTape::unitVector(const JPLocation& a, const JPLocation& b) {
    const JPLocation v = b.convertToUnits(a.units()).subtract(a);
    const double norm = 1 / a.xyzDistanceTo(b);
    return v.multiply(norm, norm, norm, 0.0);
}

double JPFeederTape::distanceToSegment(const JPLocation& p, const JPLocation& a, const JPLocation& b) {
    const JPLocation aa = a.convertToUnits(p.units()), bb = b.convertToUnits(p.units());
    const double abx = bb.x() - aa.x(), aby = bb.y() - aa.y();
    const double apx = p.x() - aa.x(), apy = p.y() - aa.y();
    const double bpx = p.x() - bb.x(), bpy = p.y() - bb.y();
    const double e = apx * abx + apy * aby;
    if (e <= 0.0) return std::sqrt(apx * apx + apy * apy);
    const double f = abx * abx + aby * aby;
    if (e >= f) return std::sqrt(bpx * bpx + bpy * bpy);
    return std::sqrt(std::max(0.0, apx * apx + apy * apy - e * e / f));
}

JPLocation JPFeederTape::transform(const std::optional<JPLocation>& visionOffset, const Params& params) {
    JPLocation unit = unitVector(params.hole1Location, params.hole2Location);
    // Holes not yet set: as if they ran along +Y.
    if (!(std::isfinite(unit.x()) && std::isfinite(unit.y()))) unit = JPLocation(params.hole1Location.units(), 0, 1, 0, 0);
    const double rotationTape = std::atan2(unit.y(), unit.x()) * 180.0 / M_PI;
    JPLocation t = params.partLocation.derive(std::nullopt, std::nullopt, std::nullopt, rotationTape);
    if (visionOffset) t = t.subtractWithRotation(*visionOffset);
    return t;
}

JPLocation JPFeederTape::feederToMachine(const JPLocation& feederLocation, const std::optional<JPLocation>& visionOffset,
                                         const Params& params) {
    const JPLocation t = transform(visionOffset, params);
    return feederLocation.rotateXy(t.rotation()).addWithRotation(t);
}

JPLocation JPFeederTape::machineToFeeder(const JPLocation& machineLocation, const std::optional<JPLocation>& visionOffset,
                                         const Params& params) {
    const JPLocation t = transform(visionOffset, params);
    return machineLocation.subtractWithRotation(t).rotateXy(-t.rotation());
}

JPLocation JPFeederTape::partLocation(long partInCycle, const std::optional<JPLocation>& visionOffset, const Params& params,
                                      double rotationInFeeder) {
    const long cycle = partsPerFeedCycle(params);
    const long offsetPitches = (cycle - partInCycle) % cycle;
    const JPLocation feederLocation(params.partPitch.units(), params.partPitch.value() * double(offsetPitches), 0, 0,
                                    rotationInFeeder);
    return feederToMachine(feederLocation, visionOffset, params);
}

} // inline namespace jf
