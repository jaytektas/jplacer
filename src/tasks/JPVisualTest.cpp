// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPVisualTest.h"
#include "JPPipelineMarkFinder.h"

#include "JPCameraLook.h"

#include "common/JPlacerLog.h"
#include "openpnp/JPXmlReader.h"
#include "pipeline/JPDefaultPipelines.h"
#include "pipeline/JPStraightPicture.h"

#include <j/core/Log.h>

#include <opencv2/imgproc.hpp>

#include <cmath>

inline namespace jf {

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
    JPFrame img;
    if (!JPCameraLook::settled(feed, img, r.why)) return r;
    if (img.width != cal.width || img.height != cal.height) {
        r.why = feed.config().name + " changed its picture size while in use";
        return r;
    }
    const double scale = std::sqrt(cal.scaleX() * cal.scaleY());
    // Found as OpenPnP's Fiducial Locator finds it: the FIDUCIAL-HOME part's pipeline, else (no fiducial vision
    // settings at all) the stock fiducial pipeline.
    JPPipeline pipeline;
    if (look->pipeline) pipeline = *look->pipeline;
    else if (JPXmlElement root; JPXmlReader::parse(JPDefaultPipelines::fiducialLocator(), root, r.why)) pipeline = JPPipeline::fromXml(root);
    // Straightened, as OpenPnP's pipelines are given pictures, and placed by the straightened picture's calibration.
    cv::Mat bgr;
    {
        cv::Mat rgba(img.height, img.width, CV_8UC4, img.rgba.data());
        cv::cvtColor(rgba, bgr, cv::COLOR_RGBA2BGR);
    }
    if (const auto straight = JPStraightPicture::of(cal, feed.config().looksUp, feed.config().showAll)) {
        cv::Mat flat;
        if (straight->straighten(bgr, flat)) {
            bgr = flat;
            cal = straight->calibration();
        }
    }
    JPPipelineMarkFinder finder(std::move(pipeline), "fiducial", cal.scaleX(), cal.scaleY());
    double ex = bgr.cols / 2.0, ey = bgr.rows / 2.0;
    cal.pixelFor(viewX, viewY, viewX, viewY, ex, ey);   // where the settings put the mark: the camera's middle
    const JPRoundMark m = finder.find(bgr, ex, ey, look->maxDistanceMm * scale, look->diameterMm * scale);
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
