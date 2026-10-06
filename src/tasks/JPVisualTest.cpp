// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPVisualTest.h"
#include "JPPipelineMarkFinder.h"

#include "JPCameraLook.h"

#include "common/JPlacerLog.h"
#include "vision/JPRoundMarkFinder.h"

#include <j/core/Log.h>

#include <cmath>

inline namespace jf {

namespace {

// How far from where it should be the mark is looked for by jplacer's finder (mm): the switches put the head
// within a fraction of this.
constexpr double kSearchMm = 2.0;

} // namespace

JPVisualTest::Result JPVisualTest::run(JPCell& cell, JPCameraFeed& feed, const JPHeadConfig& head, double speed, const Look* look) {
    Result r;
    const JPMountConfig& mount = feed.config().mount;
    if (!head.homingFiducial) {
        r.why = "the head has no homing fiducial set";
        return r;
    }
    if (!look || look->diameterMm <= 0) {
        r.why = "Visual homing is missing the FIDUCIAL-HOME part. Please create it.";
        return r;
    }
    if (mount.axisX.empty() || mount.axisY.empty()) {
        r.why = feed.config().name + " is not a camera on the head";
        return r;
    }
    JPCameraCalibration cal;
    if (!JPCameraLook::calibration(cell, feed, cal, r.why)) return r;
    // Look where the settings say the mark is (the camera's axes, less its
    // offset on the head).
    const double viewX = head.homingFiducial->x, viewY = head.homingFiducial->y;
    if (!cell.moveAxesAndWait({ { mount.axisX, viewX - mount.offsetX }, { mount.axisY, viewY - mount.offsetY } },
                              speed, r.why))
        return r;
    JPGrayImage img;
    if (!JPCameraLook::settled(feed, img, r.why)) return r;
    if (img.width != cal.width || img.height != cal.height) {
        r.why = feed.config().name + " changed its picture size while in use";
        return r;
    }
    const double scale = std::sqrt(cal.scaleX() * cal.scaleY());
    // Found as OpenPnP's Fiducial Locator finds it: the part's pipeline (its centre then measured to a fraction
    // of a pixel close by); without one (fiducials not found by pipeline), jplacer's finder.
    JPRoundMark m;
    if (look->pipeline) {
        JPPipelineMarkFinder finder(*look->pipeline, "fiducial", cal.scaleX(), cal.scaleY());
        m = finder.find(img, img.width / 2.0, img.height / 2.0, look->maxDistanceMm * scale, look->diameterMm * scale);
    } else {
        JPRoundMarkFinder::Request rq;
        rq.expectedX = img.width / 2.0;
        rq.expectedY = img.height / 2.0;
        rq.searchRadius = kSearchMm * scale;
        rq.diameter = look->diameterMm * scale;
        m = JPCameraLook::findTryingHarder(cell, feed, img, rq);
    }
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
