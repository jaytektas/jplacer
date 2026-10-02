// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPVisualHoming.h"

#include "JPVisualTest.h"

#include "common/JPlacerLog.h"

#include <j/core/Log.h>

#include <cmath>
#include <cstdio>

inline namespace jf {

namespace {

// Done when the mark measures this near its setting (mm), after at most so
// many corrections. About a machine's repeatability: correcting by less
// would chase the scatter of the moves, not the home.
constexpr double kHomedWithinMm = 0.02;
constexpr int    kCorrections   = 3;

} // namespace

JPVisualHoming::Result JPVisualHoming::run(JPCell& cell, JPCameraFeed& feed, const JPHeadConfig& head, double speed) {
    Result r;
    const JPMountConfig& mount = feed.config().mount;
    for (int i = 0; i <= kCorrections; ++i) {
        const JPVisualTest::Result t = JPVisualTest::run(cell, feed, head, speed);
        if (!t.found) {
            r.why = t.why;
            return r;
        }
        r.leftX = t.offsetX;
        r.leftY = t.offsetY;
        if (std::hypot(t.offsetX, t.offsetY) <= kHomedWithinMm) {
            r.ok = true;
            JLOGC(JPlacerLog::kCell, JLogLevel::Info) << "visual homing: corrected by " << r.correctedX << ", " << r.correctedY;
            return r;
        }
        if (i == kCorrections) break;
        // The mark measured offset from its setting: the coordinates are off by that.
        if (!cell.correctPosition({ { mount.axisX, t.offsetX }, { mount.axisY, t.offsetY } }, r.why)) return r;
        r.correctedX += t.offsetX;
        r.correctedY += t.offsetY;
    }
    char buf[160];
    std::snprintf(buf, sizeof buf, "the homing mark still measures %.3f, %.3f mm from its setting after %d corrections",
                  r.leftX, r.leftY, kCorrections);
    r.why = buf;
    return r;
}

} // inline namespace jf
