// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPRotationLook.h"

#include "JPCameraLook.h"
#include "JPPadPattern.h"

#include "common/JPlacerLog.h"
#include "vision/JPPatternFinder.h"

#include <j/core/Log.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

inline namespace jf {

namespace {

// The board is located: the part is looked for this close to where it puts it (mm).
constexpr double kSearchMm = 0.5;

} // namespace

JPRotationLook::Result JPRotationLook::run(JPCell& cell, JPCameraFeed& feed, const JPBoardSide& board, const JPPlacement& p,
                                           const JPFootprint& f, double degrees, double speed) {
    Result r;
    const JPMountConfig& mount = feed.config().mount;
    if (mount.axisX.empty() || mount.axisY.empty()) {
        r.why = feed.config().name + " is not a camera on the head";
        return r;
    }
    JPCameraCalibration cal;
    if (!JPCameraLook::calibration(cell, feed, cal, r.why)) return r;
    double vx, vy;
    board.toMachine.apply(p.x, p.y, vx, vy);
    if (!cell.moveAxesAndWait({ { mount.axisX, vx - mount.offsetX }, { mount.axisY, vy - mount.offsetY } }, speed, r.why))
        return r;
    JPGrayImage img;
    if (!JPCameraLook::settled(feed, img, r.why)) return r;
    double ex, ey;
    if (!cal.pixelFor(vx, vy, vx, vy, ex, ey)) {
        ex = img.width / 2.0;
        ey = img.height / 2.0;
    }
    const double scale = std::sqrt(cal.scaleX() * cal.scaleY());
    for (int q = 0; q < 4; ++q) {
        JPGrayImage pattern;
        double cx, cy;
        if (!JPPadPattern::draw(cal, vx, vy, board.toMachine, p, f, degrees + 90 * q, ex, ey, pattern, cx, cy)) {
            r.why = "its pads could not be drawn as the camera sees them";
            return r;
        }
        JPPatternFinder::Request rq;
        rq.expectedX = ex;
        rq.expectedY = ey;
        rq.searchRadius = kSearchMm * scale;
        rq.minScore = -1;   // every angle's score is wanted
        r.scores[size_t(q)] = std::max(0.0, JPPatternFinder::find(img, pattern, cx, cy, rq).score);
    }
    std::array<double, 4> sorted = r.scores;
    std::sort(sorted.begin(), sorted.end(), std::greater<double>());
    r.quarters = int(std::max_element(r.scores.begin(), r.scores.end()) - r.scores.begin());
    char buf[160];
    std::snprintf(buf, sizeof buf, "matches at 0/90/180/270: %.2f %.2f %.2f %.2f", r.scores[0], r.scores[1], r.scores[2], r.scores[3]);
    JLOGC(JPlacerLog::kBoard, JLogLevel::Info) << p.designator << " " << buf;
    if (sorted[0] < JPPatternFinder::Request().minScore) {
        r.why = std::string("its pads were not seen there (") + buf + ")";
        return r;
    }
    if (sorted[0] - sorted[1] < kClearMargin) {
        r.why = std::string("no angle matches clearly better than another (") + buf + "): decide it on the board";
        return r;
    }
    r.ok = true;
    return r;
}

} // inline namespace jf
