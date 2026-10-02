// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPVisualTest.h"

#include "JPCameraLook.h"

#include "common/JPlacerLog.h"
#include "vision/JPRoundMarkFinder.h"

#include <j/core/Log.h>

#include <cmath>

inline namespace jf {

namespace {

// How far from where it should be the mark is looked for (mm): the switches
// put the head within a fraction of this.
constexpr double kSearchMm = 2.0;

} // namespace

JPVisualTest::Result JPVisualTest::run(JPCell& cell, JPCameraFeed& feed, const JPHeadConfig& head, double speed) {
    Result r;
    const JPMountConfig& mount = feed.config().mount;
    const JPCameraCalibration cal = cell.cameraCalibration(feed.config().id);
    if (!head.homingFiducial || head.homingFiducialDiameter <= 0) {
        r.why = "the head has no homing mark set (its place and size)";
        return r;
    }
    if (mount.axisX.empty() || mount.axisY.empty() || !cal.valid) {
        r.why = feed.config().name + " is not a calibrated camera on the head: calibrate it first";
        return r;
    }
    // Look where the settings say the mark is (the camera's axes, less its
    // offset on the head).
    const double viewX = head.homingFiducial->x, viewY = head.homingFiducial->y;
    if (!cell.moveAxesAndWait({ { mount.axisX, viewX - mount.offsetX }, { mount.axisY, viewY - mount.offsetY } },
                              speed, r.why))
        return r;
    JPGrayImage img;
    if (!JPCameraLook::settled(feed, img, r.why)) return r;
    const double scale = std::sqrt(cal.scaleX() * cal.scaleY());
    JPRoundMarkFinder::Request rq;
    rq.expectedX = img.width / 2.0;
    rq.expectedY = img.height / 2.0;
    rq.searchRadius = kSearchMm * scale;
    rq.diameter = head.homingFiducialDiameter * scale;
    const JPRoundMark m = JPRoundMarkFinder::find(img, rq);
    if (!m.found) {
        r.why = "the homing mark was not found: " + m.why;
        return r;
    }
    cal.machinePoint(m.x, m.y, viewX, viewY, r.markX, r.markY);
    r.offsetX = r.markX - head.homingFiducial->x;
    r.offsetY = r.markY - head.homingFiducial->y;
    r.confidence = m.confidence;
    r.found = true;
    JLOGC(JPlacerLog::kCamera, JLogLevel::Info) << "visual test: homing mark at " << r.markX << ", " << r.markY
                                                << " (" << r.offsetX << ", " << r.offsetY << " from its setting)";
    return r;
}

} // inline namespace jf
