// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPRunoutCalibrator.h"

#include <opencv2/imgproc.hpp>

#include "JPCameraLook.h"
#include "machine/JPScripting.h"

#include "common/JPlacerLog.h"
#include "JPPipelineMarkFinder.h"
#include "pipeline/JPDefaultPipelines.h"

#include <j/core/Log.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>

inline namespace jf {

namespace {

// With no size known, the end is looked for between these (mm).
constexpr double kLeastTipMm = 0.2, kMostTipMm = 4.0;

std::string now() {
    const std::time_t t = std::time(nullptr);
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", std::localtime(&t));
    return buf;
}

} // namespace

std::optional<JPRunout> JPRunoutCalibrator::run(JPCell& cell, JPCameraFeed& camera, const JPNozzleConfig& nozzle,
                                               const JPNozzleTipConfig& tip, const Options& o, std::string& why,
                                               const Progress& progress, JPBackgroundCalibration* background) {
    const JPCameraConfig& cam = camera.config();
    const JPMountConfig& m = nozzle.mount;
    if (!cam.mount.headId.empty()) {
        why = cam.name + " rides on a head: runout is measured with a camera fixed to the machine, looking up";
        return std::nullopt;
    }
    if (m.axisX.empty() || m.axisY.empty() || m.axisZ.empty() || m.axisRotation.empty()) {
        why = nozzle.name + " does not move on X, Y, Z and rotation";
        return std::nullopt;
    }
    if (!cell.isHomed()) {
        why = "home the machine first";
        return std::nullopt;
    }
    JPCameraCalibration cal;
    if (!JPCameraLook::calibration(cell, camera, cal, why)) return std::nullopt;
    const JPNozzleTipConfig::RunoutCalibration& k = tip.runoutCalibration;
    const int divisions = std::clamp(k.divisions, JPNozzleTipConfig::RunoutCalibration::kLeastDivisions,
                                     JPNozzleTipConfig::RunoutCalibration::kMostDivisions);
    const double scale = cal.scale();
    const double diameter = k.visionDiameter > 0 ? k.visionDiameter : tip.diameter;
    // Looked for beyond the threshold, so a tip found there is seen (and refused) rather than missed.
    const double search = k.offsetThresholdMm * (1 + JPNozzleTipConfig::RunoutCalibration::kDetectionMargin);
    // Where the camera looks, and the axes that put the tip's axis there.
    const double camX = cam.mount.offsetX, camY = cam.mount.offsetY;
    const double ax = camX - m.offsetX, ay = camY - m.offsetY, az = cam.mount.offsetZ + k.zOffset - m.offsetZ;

    std::vector<JPRunout::Point> points;
    int failed = 0;
    JPPipelineMarkFinder finder(k.pipeline.empty() ? JPDefaultPipelines::nozzleTipCalibration() : k.pipeline, "nozzleTip");
    bool ok = cell.safeZAndWait(m.headId, o.speed, why)
           && cell.moveAxesAndWait({ { m.axisX, ax }, { m.axisY, ay }, { m.axisRotation, -180 } }, o.speed, why)
           && cell.moveAxesAndWait({ { m.axisZ, az } }, o.speed, why);
    for (int i = 0; ok && i < divisions; ++i) {
        const double angle = -180 + 360.0 * i / divisions;
        char said[64];
        std::snprintf(said, sizeof said, "turned to %.0f deg (%d of %d)", angle, i + 1, divisions);
        if (progress) progress(said);
        if (!cell.moveAxesAndWait({ { m.axisRotation, angle } }, o.speed, why)) {
            ok = false;
            break;
        }
        JPGrayImage img;
        if (!JPCameraLook::settled(camera, img, why)) {
            ok = false;
            break;
        }
        double ex, ey;
        if (!cal.pixelFor(camX, camY, camX, camY, ex, ey)) {
            ex = img.width / 2.0;
            ey = img.height / 2.0;
        }
        // The tip found by its calibration pipeline (OpenPnP's, editable), under the "nozzleTip" properties.
        JPRoundMark found = diameter > 0
                                      ? finder.find(img, ex, ey, search * scale, diameter * scale)
                                      : finder.findAnySize(img, ex, ey, search * scale, kLeastTipMm * scale, kMostTipMm * scale);
        double tx, ty;
        if (found.found && cal.machinePoint(found.x, found.y, camX, camY, tx, ty)
            && std::hypot(tx - camX, ty - camY) > k.offsetThresholdMm) {
            // Beyond the Offset Threshold: a misdetect.
            char far[96];
            std::snprintf(far, sizeof far, "found %.3f mm off, beyond the offset threshold of %.3f mm",
                          std::hypot(tx - camX, ty - camY), k.offsetThresholdMm);
            found.found = false;
            found.why = far;
        }
        if (!found.found || !cal.machinePoint(found.x, found.y, camX, camY, tx, ty)) {
            JLOGC(JPlacerLog::kCamera, JLogLevel::Warn) << tip.name << " on " << nozzle.name << " not found at " << angle
                                                        << " deg: " << found.why;
            if (++failed > k.misdetects) {
                // OpenPnP's words, and what the last one was.
                why = "Nozzle tip " + tip.name + " on " + nozzle.name + " calibration: too many vision misdetects. Check the "
                      "allowable distance threshold and/or computer vision. (" + found.why + ")";
                ok = false;
            }
            continue;
        }
        points.push_back({ angle, tx - camX, ty - camY });
        // The picture for the background: the tip's middle blotted out (the smallest part it picks,
        // less the pick tolerance, but no smaller than the tip), blurred to the smallest detail.
        if (background) {
            JPFrame frame;
            if (camera.latest(frame, 0) && frame.width > 0) {
                cv::Mat rgba(frame.height, frame.width, CV_8UC4, frame.rgba.data()), bgr;
                cv::cvtColor(rgba, bgr, cv::COLOR_RGBA2BGR);
                const double blot = std::max(tip.minPartDiameterMm - 2 * tip.maxPickToleranceMm, tip.diameter);
                background->add(bgr, found.x, found.y, int(std::ceil(blot * scale * 0.5)),
                                int(tip.background.minimumDetailSizeMm * scale));
            }
        }
    }
    // Up again, whatever happened.
    std::string up;
    if (!cell.safeZAndWait(m.headId, o.speed, up) && why.empty()) why = up;
    if (!ok) return std::nullopt;
    auto r = JPRunout::fit(points);
    if (!r) {
        why = "too few angles measured to fit the circle";
        return std::nullopt;
    }
    r->when = now();
    JLOGC(JPlacerLog::kCamera, JLogLevel::Info) << tip.name << " on " << nozzle.name << ": runout " << r->radius
        << " mm at " << r->phaseDeg << " deg, axis off by " << r->centreX << ", " << r->centreY << ", fit " << r->rmsMm;
    return r;
}

std::optional<JPRunout> JPRunoutCalibrator::measure(JPCell& cell, JPCameraFeed& feed, const JPNozzleConfig& n,
                                                    const JPNozzleTipConfig& t, JPScripting* scripting, std::string& words,
                                                    const Progress& progress,
                                                    std::optional<JPBackgroundCalibration::Result>& background) {
    const Options o;
    // The background calibrated along with it, when the tip asks for it.
    JPBackgroundCalibration pictures(JPBackgroundCalibration::methodFrom(t.background.method));
    const bool withBackground = t.background.method != "None";
    // OpenPnP's NozzleCalibration.Starting before it, and .Finished once the tip was found enough.
    JJson g = JJson::object();
    g["nozzle"] = n.name;
    g["camera"] = feed.config().name;
    if (scripting && !scripting->on("NozzleCalibration.Starting", g, words)) return std::nullopt;
    const auto r = run(cell, feed, n, t, o, words, progress, withBackground ? &pictures : nullptr);
    if (!r) return std::nullopt;
    if (scripting && !scripting->on("NozzleCalibration.Finished", g, words)) return std::nullopt;
    JPCameraCalibration cal;
    std::string ignored;
    JPBackgroundCalibration::Result b;
    if (withBackground && JPCameraLook::calibration(cell, feed, cal, ignored)
        && pictures.finish((t.maxPartDiameterMm + 2 * t.maxPickToleranceMm) * cal.scale() * 0.5, b))
        background = b;
    char buf[200];
    std::snprintf(buf, sizeof buf, "%s on %s: runout %.3f mm at %.1f deg; its axis %+.3f, %+.3f mm off; fit %.4f mm",
                  t.name.c_str(), n.name.c_str(), r->radius, r->phaseDeg, r->centreX, r->centreY, r->rmsMm);
    words = buf;
    return r;
}

} // inline namespace jf
