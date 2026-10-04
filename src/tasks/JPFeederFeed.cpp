// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPFeederFeed.h"

#include "common/JPlacerLog.h"

#include <j/core/Log.h>

#include <cmath>
#include <cstdlib>

inline namespace jf {

namespace {

// A hole found farther than this from where it should be is not taken (mm).
constexpr double kMostOffMm = 2.0;
constexpr const char* kEndOfStrip = "Unable to locate reference hole. End of strip?";

} // namespace

bool JPFeederFeed::feed(JPConfiguration& config, const std::string& feederId, JPJobMachine& machine, const OnMain& onMain,
                        std::string& why, bool& empty) {
    auto main = [&onMain](const std::function<void()>& fn) {
        if (onMain) onMain(fn);
        else fn();
    };
    bool fed = false;
    empty = false;
    std::vector<int> checks;
    main([&] {
        JPFeeder* f = config.feeder(feederId);
        if (!f) {
            why = "no feeder " + feederId;
            return;
        }
        fed = f->feed(why, &empty);
        checks = f->takeVisionChecks();
    });
    if (!fed) return false;
    for (const int n : checks) {
        std::optional<JPLocation> expected;
        double diameter = 0, search = 0, parallaxDiameter = 0, parallaxAngle = 0;
        std::string name;
        main([&] {
            if (const JPFeeder* f = config.feeder(feederId)) {
                expected = f->visionExpected(n);
                diameter = f->holeDiameter().convertToUnits(JPLengthUnit::Millimeters).value();
                search = f->holePitch().convertToUnits(JPLengthUnit::Millimeters).value() / 2;
                parallaxDiameter = f->lengthOf("parallax-diameter", JPLength(0, JPLengthUnit::Millimeters))
                                       .convertToUnits(JPLengthUnit::Millimeters)
                                       .value();
                parallaxAngle = std::strtod(f->childText("parallax-angle", "0").c_str(), nullptr);
                name = f->name();
            }
        });
        if (!expected) continue;
        JPLocation found(JPLengthUnit::Millimeters);
        std::string seen;
        const bool ok = machine.locateHole(*expected, diameter, search, parallaxDiameter, parallaxAngle, found, seen);
        const JPLocation e = expected->convertToUnits(JPLengthUnit::Millimeters);
        if (!ok || std::hypot(found.x() - e.x(), found.y() - e.y()) > kMostOffMm) {
            JLOGC(JPlacerLog::kJob, JLogLevel::Info) << name << ": hole " << n << " not found" << (ok ? "" : ": " + seen);
            why = kEndOfStrip;
            empty = true;
            return false;
        }
        main([&] {
            if (JPFeeder* f = config.feeder(feederId)) f->setVisionFound(n, found);
        });
    }
    return true;
}

} // inline namespace jf
