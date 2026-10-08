// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPVisualHoming.h"

#include "JPFiducialLocator.h"
#include "JPVisualTest.h"

#include "common/JPlacerLog.h"

#include <j/core/Log.h>

#include <cmath>

inline namespace jf {

namespace {

// OpenPnP's homing fiducial: the part of this id.
constexpr const char* kHomePart = "FIDUCIAL-HOME";

} // namespace

JPVisualHoming::Result JPVisualHoming::run(JPCell& cell, JPCameraFeed& feed, const JPHeadConfig& head, double speed,
                                           const JPVisualTest::Look* look) {
    Result r;
    const JPMountConfig& mount = feed.config().mount;
    const int passes = look ? std::max(1, look->passes) : 1;
    for (int i = 0; i < passes; ++i) {
        const JPVisualTest::Result t = JPVisualTest::run(cell, feed, head, speed, look);
        if (!t.found) {
            r.why = t.why;
            return r;
        }
        r.leftX = t.offsetX;
        r.leftY = t.offsetY;
        // The mark measured offset from its setting: the coordinates are off by that.
        if (!cell.correctPosition({ { mount.axisX, t.offsetX }, { mount.axisY, t.offsetY } }, r.why)) return r;
        r.correctedX += t.offsetX;
        r.correctedY += t.offsetY;
        if (!look || std::hypot(t.offsetX, t.offsetY) < look->maxLinearOffsetMm) break;   // as OpenPnP's: the locator satisfied
    }
    r.ok = true;
    JLOGC(JPlacerLog::kCell, JLogLevel::Info) << "visual homing: corrected by " << r.correctedX << ", " << r.correctedY
                                              << " (the last find " << r.leftX << ", " << r.leftY << ")";
    return r;
}

std::optional<JPFootprint> JPVisualHoming::homeFootprint(JPConfiguration& configuration) {
    const JPPart* part = configuration.part(kHomePart);
    const JPPackage* k = part ? configuration.package(part->packageId) : nullptr;
    if (!k || k->footprint.pads.empty()) return std::nullopt;
    return k->footprint;
}

std::optional<JPVisualTest::Look> JPVisualHoming::homeLook(JPConfiguration& configuration, const JPVisionConfig& vision) {
    const JPPart* part = configuration.part(kHomePart);
    if (!part) return std::nullopt;
    double diameter = 0;
    JPJobMachine::FiducialLook look;
    std::string settings;
    if (JPFiducialLocator::partLook(configuration, *part, vision, diameter, look, settings) != JPFiducialLocator::PartProblem::None)
        return std::nullopt;
    return JPVisualTest::Look { diameter, look.pipeline, look.passes, look.maxLinearOffsetMm, vision.fiducialMaxDistanceMm,
                                homeFootprint(configuration) };
}

} // inline namespace jf
