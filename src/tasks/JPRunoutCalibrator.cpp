// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPRunoutCalibrator.h"

#include <opencv2/imgproc.hpp>

#include "JPCameraLook.h"
#include "JPRunoutFit.h"
#include "model/JPFiducialFit.h"
#include "machine/JPScripting.h"

#include "common/JPlacerLog.h"
#include "JPPipelineMarkFinder.h"
#include "pipeline/JPDefaultPipelines.h"
#include "pipeline/JPStraightPicture.h"

#include <j/core/Log.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <ctime>

inline namespace jf {

namespace {

// With no size known, the end is looked for between these (mm).
constexpr double kLeastTipMm = 0.2, kMostTipMm = 4.0;

// The tip's end found in a settled picture by the calibration pipeline (OpenPnP's findCircle): looked for about
// where it is expected (expectX, expectY, mm), up to the Offset Threshold and its margin; one further than the
// threshold from there is a misdetect. Where it is on the machine into tx, ty.
JPRoundMark findTip(JPPipelineMarkFinder& finder, const cv::Mat& img, const JPCameraCalibration& cal, double camX, double camY,
                    double expectX, double expectY, double diameter, double searchMm, double thresholdMm, double& tx, double& ty) {
    const double scale = cal.scale();
    double ex, ey;
    if (!cal.pixelFor(expectX, expectY, camX, camY, ex, ey)) {
        ex = img.cols / 2.0;
        ey = img.rows / 2.0;
    }
    // By its calibration pipeline (OpenPnP's, editable), under the "nozzleTip" properties.
    JPRoundMark found = diameter > 0 ? finder.find(img, ex, ey, searchMm * scale, diameter * scale)
                                     : finder.findAnySize(img, ex, ey, searchMm * scale, kLeastTipMm * scale, kMostTipMm * scale);
    if (found.found && cal.machinePoint(found.x, found.y, camX, camY, tx, ty)) {
        if (const double off = std::hypot(tx - expectX, ty - expectY); off > thresholdMm) {
            // Beyond the Offset Threshold: a misdetect.
            char far[96];
            std::snprintf(far, sizeof far, "found %.3f mm off, beyond the offset threshold of %.3f mm", off, thresholdMm);
            found.found = false;
            found.why = far;
        }
    } else {
        found.found = false;
    }
    return found;
}

// A picture as a vision pipeline is given it (OpenPnP's): in colour, straightened where `straight` is.
cv::Mat pipelinePicture(const JPFrame& frame, const JPStraightPicture* straight) {
    cv::Mat rgba(frame.height, frame.width, CV_8UC4, const_cast<uint8_t*>(frame.rgba.data())), bgr;
    cv::cvtColor(rgba, bgr, cv::COLOR_RGBA2BGR);
    cv::Mat flat;
    if (straight && straight->straighten(bgr, flat)) return flat;
    return bgr;
}

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
    // Its pictures straightened, as OpenPnP's pipelines are given them; placed by their calibration.
    const auto straight = JPStraightPicture::of(cal, camera.config().looksUp, camera.config().showAll);
    if (straight) cal = straight->calibration();
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
        JPFrame img;
        if (!JPCameraLook::settled(camera, img, why)) {
            ok = false;
            break;
        }
        double tx = 0, ty = 0;
        const cv::Mat picture = pipelinePicture(img, straight.get());
        const JPRoundMark found = findTip(finder, picture, cal, camX, camY, camX, camY, diameter, search, k.offsetThresholdMm, tx, ty);
        if (!found.found) {
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
        if (o.found) o.found(picture, found.x, found.y, found.diameter, said);
        points.push_back({ angle, tx - camX, ty - camY });
        // The picture for the background: the tip's middle blotted out (the smallest part it picks,
        // less the pick tolerance, but no smaller than the tip), blurred to the smallest detail.
        if (background) {
            if (!picture.empty()) {
                const cv::Mat& bgr = picture;
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
    auto r = JPRunoutFit::fit(points, k.algorithm);
    if (!r) {
        why = "too few angles measured to fit the circle";
        return std::nullopt;
    }
    r->when = now();
    JLOGC(JPlacerLog::kCamera, JLogLevel::Info) << tip.name << " on " << nozzle.name << ": runout " << r->radius
        << " mm at " << r->phaseDeg << " deg, axis off by " << r->centreX << ", " << r->centreY << ", fit " << r->rmsMm;
    return r;
}

std::optional<JPRunoutCalibrator::CameraFix> JPRunoutCalibrator::calibrateCamera(JPCell& cell, JPCameraFeed& camera,
                                                                                 const JPNozzleConfig& nozzle, const JPNozzleTipConfig& tip,
                                                                                 const Options& o, std::string& why, const Progress& progress) {
    const JPCameraConfig& cam = camera.config();
    const JPMountConfig& m = nozzle.mount;
    if (!cam.mount.headId.empty() || !cam.looksUp) {
        why = cam.name + " is not a camera fixed to the machine, looking up";
        return std::nullopt;
    }
    if (!cell.isHomed()) {
        why = "home the machine first";
        return std::nullopt;
    }
    if (!tip.runoutOn(nozzle.id)) {
        why = "Calibrate the nozzle tip first.";
        return std::nullopt;
    }
    JPCameraCalibration cal;
    if (!JPCameraLook::calibration(cell, camera, cal, why)) return std::nullopt;
    // Its pictures straightened, as OpenPnP's pipelines are given them; placed by their calibration.
    const auto straight = JPStraightPicture::of(cal, camera.config().looksUp, camera.config().showAll);
    if (straight) cal = straight->calibration();
    const JPNozzleTipConfig::RunoutCalibration& k = tip.runoutCalibration;
    const int divisions = std::clamp(k.divisions, JPNozzleTipConfig::RunoutCalibration::kLeastDivisions,
                                     JPNozzleTipConfig::RunoutCalibration::kMostDivisions);
    const double diameter = k.visionDiameter > 0 ? k.visionDiameter : tip.diameter;
    const double search = k.offsetThresholdMm * (1 + JPNozzleTipConfig::RunoutCalibration::kDetectionMargin);
    const double camX = cam.mount.offsetX, camY = cam.mount.offsetY, z = cam.mount.offsetZ + k.zOffset;
    // The excenter: the picture's middle, that share of its smaller side to the right, on the machine.
    double excenterX = 0, excenterY = 0;
    {
        double mx, my, px, py;
        const double side = std::min(cal.width, cal.height) * k.excenterRatio;
        if (!cal.pixelFor(camX, camY, camX, camY, px, py) || !cal.machinePoint(px + side, py, camX, camY, mx, my)) {
            why = cam.name + "'s calibration cannot place its picture on the machine";
            return std::nullopt;
        }
        excenterX = mx - camX;
        excenterY = my - camY;
    }
    std::vector<JPLocation> seen, sent;
    std::vector<JPRunout::Point> points;
    int failed = 0;
    JPPipelineMarkFinder finder(k.pipeline.empty() ? JPDefaultPipelines::nozzleTipCalibration() : k.pipeline, "nozzleTip");
    bool ok = true;
    for (int i = 0; ok && i < divisions; ++i) {
        const double angle = -180 + 360.0 * i / divisions, a = angle * M_PI / 180;
        // Where the tip is sent (its runout compensated, as OpenPnP's nozzle.moveTo).
        const double sx = camX + excenterX * std::cos(a) - excenterY * std::sin(a);
        const double sy = camY + excenterX * std::sin(a) + excenterY * std::cos(a);
        char said[64];
        std::snprintf(said, sizeof said, "turned to %.0f deg (%d of %d)", angle, i + 1, divisions);
        if (progress) progress(said);
        const std::array<std::optional<double>, 4> to { sx, sy, z, angle };
        if (!(i == 0 ? cell.moveToolAndWait(m, to, o.speed, why) : cell.moveToolStraightAndWait(m, to, o.speed, why))) {
            ok = false;
            break;
        }
        JPFrame img;
        if (!JPCameraLook::settled(camera, img, why)) {
            ok = false;
            break;
        }
        double tx = 0, ty = 0;
        const cv::Mat picture = pipelinePicture(img, straight.get());
        const JPRoundMark found = findTip(finder, picture, cal, camX, camY, sx, sy, diameter, search, k.offsetThresholdMm, tx, ty);
        if (!found.found) {
            JLOGC(JPlacerLog::kCamera, JLogLevel::Warn) << tip.name << " on " << nozzle.name << " not found at " << angle
                                                        << " deg: " << found.why;
            if (++failed > k.misdetects) {
                why = "Nozzle tip " + tip.name + " on " + nozzle.name + " calibration: too many vision misdetects. Check the "
                      "allowable distance threshold and/or computer vision. (" + found.why + ")";
                ok = false;
            }
            continue;
        }
        if (o.found) o.found(picture, found.x, found.y, found.diameter, said);
        // Seen from the camera's middle; sent, on the machine.
        seen.emplace_back(JPLengthUnit::Millimeters, tx - camX, ty - camY, 0, angle);
        sent.emplace_back(JPLengthUnit::Millimeters, sx, sy, 0, angle);
        points.push_back({ angle, tx - camX, ty - camY });
    }
    // Over the camera, where it now is, and up again, whatever happened.
    std::string up;
    if (!cell.safeZAndWait(m.headId, o.speed, up) && why.empty()) why = up;
    if (!ok) return std::nullopt;
    if (points.size() < std::max<size_t>(3, size_t(divisions - k.misdetects))) {
        why = "Nozzle tip " + tip.name + " on " + nozzle.name + " calibration: too many vision misdetects. Check the "
              "allowable distance threshold and/or computer vision.";
        return std::nullopt;
    }
    CameraFix fix;
    fix.points = int(points.size());
    if (k.algorithm.find("Affine") != std::string::npos) {
        // What was seen onto where it was sent: its translation where the camera's middle is, its rotation the turn.
        const JPAffineTransform t = JPFiducialFit::derive(seen, sent);
        const JPAffineTransform::Info info = t.info();
        fix.x = info.xTranslation;
        fix.y = info.yTranslation;
        fix.turnDeg = info.rotationAngleDeg;
        double sum = 0;
        for (size_t i = 0; i < seen.size(); ++i) {
            double ox, oy;
            t.apply(seen[i].x(), seen[i].y(), ox, oy);
            sum += std::pow(ox - sent[i].x(), 2) + std::pow(oy - sent[i].y(), 2);
        }
        fix.rmsMm = std::sqrt(sum / double(seen.size()));
    } else {
        // The circle through what was seen: its centre the camera's error, its phase the turn (less the excenter's
        // own direction, which OpenPnP's leaves in: a camera looking up sees it along -X, half a turn).
        const auto circle = JPRunoutFit::fit(points, "Model");
        if (!circle) {
            why = "too few angles measured to fit the circle";
            return std::nullopt;
        }
        fix.x = camX - circle->centreX;
        fix.y = camY - circle->centreY;
        fix.turnDeg = std::remainder(circle->phaseDeg + std::atan2(excenterY, excenterX) * 180 / M_PI, 360.0);
        fix.rmsMm = circle->rmsMm;
    }
    JLOGC(JPlacerLog::kCamera, JLogLevel::Info) << cam.name << " by " << tip.name << " on " << nozzle.name << ": at " << fix.x << ", "
                                                << fix.y << " (was " << camX << ", " << camY << "), turned " << fix.turnDeg
                                                << " deg, fit " << fix.rmsMm;
    return fix;
}

std::optional<JPRunout> JPRunoutCalibrator::measure(JPCell& cell, JPCameraFeed& feed, const JPNozzleConfig& n,
                                                    const JPNozzleTipConfig& t, JPScripting* scripting, std::string& words,
                                                    const Progress& progress,
                                                    std::optional<JPBackgroundCalibration::Result>& background,
                                                    const Found& found) {
    Options o;
    o.found = found;
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
