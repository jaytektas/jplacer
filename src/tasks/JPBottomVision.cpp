// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPBottomVision.h"

#include "common/JPlacerLog.h"
#include "machine/JPCameraCalibration.h"
#include "model/JPFootprint.h"
#include "model/JPLength.h"
#include "model/JPVisionSettings.h"
#include "pipeline/JPPipeline.h"
#include "pipeline/JPStageUtil.h"
#include "tasks/JPVisionComposite.h"
#include "tasks/JPVisionPipelinePrep.h"

#include <j/core/Log.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdio>

inline namespace jf {

namespace {

std::string mm(double v) {
    return JPLength(v, JPLengthUnit::Millimeters).text("%.3f%s");
}

double angleOffsetNorm(const JPBottomVision::Settings& s, double angleOffset) {
    // Most pipelines tell a rectangle's angle only within 90° (no side told from another): a part is never picked
    // more than ±45° turned. With Rotation Full, within ±180° (turning further one way makes no sense).
    return s.fullRotation ? JPStageUtil::angleNorm(angleOffset, 180) : JPStageUtil::angleNorm(angleOffset);
}

// The rectangle's first corner (as OpenCV numbers them) on the machine, from the camera's middle.
JPLocation corner(const JPBottomVision::Seen& seen, double cameraX, double cameraY) {
    return JPLocation(JPLengthUnit::Millimeters, -seen.widthMm / 2, -seen.heightMm / 2, 0, 0)
        .rotateXy(seen.angle)
        .add(JPLocation(JPLengthUnit::Millimeters, seen.x - cameraX, seen.y - cameraY, 0, 0));
}

} // namespace

bool JPBottomVision::findOffsets(const Settings& s, double cameraX, double cameraY, const Look& look, Offset& out,
                                 std::string& why) {
    Seen seen;
    if (!s.preRotate) {
        const JPLocation wanted(JPLengthUnit::Millimeters, cameraX, cameraY, 0, 0);
        if (!look(wanted, 0, 0, seen, why)) return false;
        // The physical distance from the camera's middle to the part.
        JPLocation offsets(JPLengthUnit::Millimeters, seen.x - cameraX, seen.y - cameraY, 0, 0);
        const double angleOffset = angleOffsetNorm(s, seen.angle);
        if (!partSizeCheck(s, seen, why)) return false;
        offsets = offsets.derive(std::nullopt, std::nullopt, std::nullopt, angleOffset);
        offsets = offsets.subtract(s.visionOffset.rotateXy(offsets.rotation()));
        if (!offsetsCheck(s, offsets, why)) return false;
        out = { offsets, false };
        return true;
    }
    const double wantedAngle = JPStageUtil::angleNorm(s.wantedAngle, 180);
    const JPLocation wanted(JPLengthUnit::Millimeters, cameraX, cameraY, 0, wantedAngle);
    JPLocation nozzle = wanted;
    const JPLocation center(JPLengthUnit::Millimeters);
    JPLocation offsets(JPLengthUnit::Millimeters);
    // Getting a good fix on the part in passes.
    for (int pass = 0;;) {
        if (!look(nozzle, wantedAngle, pass, seen, why)) return false;
        offsets = JPLocation(JPLengthUnit::Millimeters, seen.x - cameraX, seen.y - cameraY, 0, 0);
        const double angleOffset = angleOffsetNorm(s, seen.angle - wantedAngle);
        // Turning the nozzle to make up the angle turns the off-centre part about the nozzle's axis too.
        offsets = offsets.rotateXy(-angleOffset).derive(std::nullopt, std::nullopt, std::nullopt, angleOffset);
        nozzle = nozzle.subtractWithRotation(offsets);
        if (++pass >= s.maxVisionPasses) break;
        // The corner's offset the angle brings about counts too, so a large part reacts more to an angular offset.
        const JPLocation c = corner(seen, cameraX, cameraY);
        const JPLocation cornerWithAngularOffset = c.rotateXy(angleOffset);
        if (center.linearDistanceTo(offsets) > s.maxLinearOffsetMm) {
            JLOGC(JPlacerLog::kJob, JLogLevel::Debug) << "Offsets too large: center offset " << center.linearDistanceTo(offsets)
                                                      << " > " << s.maxLinearOffsetMm;
        } else if (c.linearDistanceTo(cornerWithAngularOffset) > s.maxLinearOffsetMm) {
            JLOGC(JPlacerLog::kJob, JLogLevel::Debug) << "Offsets too large: corner offset " << c.linearDistanceTo(cornerWithAngularOffset)
                                                      << " > " << s.maxLinearOffsetMm;
        } else if (std::abs(angleOffset) > s.maxAngularOffset) {
            JLOGC(JPlacerLog::kJob, JLogLevel::Debug) << "Offsets too large: angle offset " << std::abs(angleOffset) << " > "
                                                      << s.maxAngularOffset;
        } else {
            break;   // a good enough fix
        }
    }
    // The offsets over all the passes, less the part's vision offset.
    offsets = wanted.subtractWithRotation(nozzle);
    offsets = offsets.subtract(s.visionOffset.rotateXy(wantedAngle));
    if (!offsetsCheck(s, offsets, why)) return false;
    if (!partSizeCheck(s, seen, why)) return false;
    out = { offsets, true };
    return true;
}

bool JPBottomVision::offsetsCheck(const Settings& s, const JPLocation& offsets, std::string& why) {
    if (!s.maxPickToleranceMm) return true;
    const double length = offsets.linearDistanceTo(JPLocation::origin());
    if (length > *s.maxPickToleranceMm) {
        why = "Part " + s.partId + " bottom vision offsets length " + mm(length) + " larger than the allowed Max. Pick Tolerance "
            + mm(*s.maxPickToleranceMm) + " set on nozzle tip " + s.tipName + ".";
        return false;
    }
    return true;
}

bool JPBottomVision::partSizeCheck(const Settings& s, const Seen& seen, std::string& why) {
    if (!s.partCheckSizeMm) return true;
    const auto [width, length] = *s.partCheckSizeMm;
    if (!(seen.widthMm > 0 && seen.heightMm > 0)) {
        char buf[160];
        std::snprintf(buf, sizeof buf, "Invalid measured size for size check.\nwidth=%g\nlength=%g", seen.widthMm, seen.heightMm);
        why = buf;
        return false;
    }
    const double widthTolerance = width * 0.01 * s.checkSizeTolerancePercent;
    const double lengthTolerance = length * 0.01 * s.checkSizeTolerancePercent;
    std::string msg;
    bool ok = false;
    if (seen.widthMm > width + widthTolerance)
        msg = "Part " + s.partId + " width too large: nominal " + mm(width) + ", limit " + mm(width + widthTolerance) + ", measured " + mm(seen.widthMm);
    else if (seen.widthMm < width - widthTolerance)
        msg = "Part " + s.partId + " width too small: nominal " + mm(width) + ", limit " + mm(width - widthTolerance) + ", measured " + mm(seen.widthMm);
    else if (seen.heightMm > length + lengthTolerance)
        msg = "Part " + s.partId + " length too large: nominal " + mm(length) + ", limit " + mm(length + lengthTolerance) + ", measured " + mm(seen.heightMm);
    else if (seen.heightMm < length - lengthTolerance)
        msg = "Part " + s.partId + " length too small: nominal " + mm(length) + ", limit " + mm(length - lengthTolerance) + ", measured " + mm(seen.heightMm);
    else {
        msg = "Part " + s.partId + " size ok. Width " + mm(seen.widthMm) + ", Length " + mm(seen.heightMm);
        ok = true;
    }
    JLOGC(JPlacerLog::kJob, JLogLevel::Debug) << msg;
    if (!ok) why = msg;
    return ok;
}

std::optional<std::pair<double, double>> JPBottomVision::partCheckSize(const JPVisionSettings& settings, const JPFootprint& footprint,
                                                                       bool addTolerance) {
    const std::string method = settings.text("check-part-size-method", "Disabled");
    double w = 0, h = 0;
    if (method == "BodySize") {
        w = footprint.bodyWidth;
        h = footprint.bodyHeight;
    } else if (method == "PadExtents") {
        double x0 = DBL_MAX, y0 = DBL_MAX, x1 = -DBL_MAX, y1 = -DBL_MAX;
        for (const JPFootprint::Outline& o : footprint.padsOutlines())
            for (const JPFootprint::Point& p : o) {
                x0 = std::min(x0, p.x);
                y0 = std::min(y0, p.y);
                x1 = std::max(x1, p.x);
                y1 = std::max(y1, p.y);
            }
        if (x1 >= x0) {
            w = x1 - x0;
            h = y1 - y0;
        }
    } else {
        return std::nullopt;
    }
    const double factor = addTolerance ? settings.number("check-size-tolerance-percent", 20) * 0.01 + 1.0 : 1.0;
    const double unit = JPLength(1, footprint.units).convertToUnits(JPLengthUnit::Millimeters).value();
    return std::pair { w * factor * unit, h * factor * unit };
}

bool JPBottomVision::seenOf(const cv::RotatedRect& rect, const JPCameraCalibration& cal, double cameraX, double cameraY, double angle,
                            double range, Seen& seen, std::string& why) {
    const double x = rect.center.x, y = rect.center.y, a = rect.angle * M_PI / 180;
    double ax = 0, ay = 0, bx = 0, by = 0;
    if (!cal.machinePoint(x, y, cameraX, cameraY, ax, ay)
        || !cal.machinePoint(x + kAngleStepPx * std::cos(a), y + kAngleStepPx * std::sin(a), cameraX, cameraY, bx, by)) {
        why = "the camera's calibration cannot place the part";
        return false;
    }
    const double along = std::atan2(by - ay, bx - ax) * 180 / M_PI;
    seen.x = ax;
    seen.y = ay;
    seen.angle = angle + JPStageUtil::angleNorm(along - angle, range);
    // The rectangle's sides as the pipeline turned it, nearest the angle wanted.
    seen.widthMm = rect.size.width / cal.scale();
    seen.heightMm = rect.size.height / cal.scale();
    return true;
}

bool JPBottomVision::findByPipeline(JPPipeline& p, const std::string& partId, const JPCameraCalibration& cal, double cameraX,
                                    double cameraY, double nozzleX, double nozzleY, double angle, double range, Seen& seen,
                                    std::string& why) {
    double expectedX = 0, expectedY = 0;
    if (!cal.pixelFor(nozzleX, nozzleY, cameraX, cameraY, expectedX, expectedY)) {
        why = "the camera's calibration cannot place the nozzle in its picture";
        return false;
    }
    const JPPipelineValue centre { JPPipelineValue::Pixel { expectedX, expectedY } };
    p.setProperty("MinAreaRect.center", centre);
    p.setProperty("DetectRectlinearSymmetry.center", centre);
    const double pictureAngle = p.context().pictureAngle(angle);
    p.setProperty("MinAreaRect.expectedAngle", JPPipelineValue { pictureAngle });
    p.setProperty("DetectRectlinearSymmetry.expectedAngle", JPPipelineValue { pictureAngle });
    cv::RotatedRect rect;
    if (!resultRect(p, partId, rect, why)) return false;
    return seenOf(rect, cal, cameraX, cameraY, angle, range, seen, why);
}

bool JPBottomVision::seeComposite(const Composite& k, double nx, double ny, double angle, Seen& seen, std::string& why) {
    const double c = std::cos(angle * M_PI / 180), s = std::sin(angle * M_PI / 180);
    auto turned = [c, s](double x, double y) { return std::pair { c * x - s * y, s * x + c * y }; };
    // Where the nozzle is, as the part's frame sees it (from the part's centre over the camera).
    double hereX = 0, hereY = 0;
    if (!k.nozzleAt(hereX, hereY)) {
        why = "the nozzle's place is not known";
        return false;
    }
    const double fromX = c * (hereX - nx) + s * (hereY - ny), fromY = -s * (hereX - nx) + c * (hereY - ny);
    k.composite.restart();
    for (const JPVisionComposite::Shot* shot : k.composite.travel(fromX, fromY)) {
        // The nozzle so the shot's middle is over the camera.
        const auto [sx, sy] = turned(shot->x, shot->y);
        if (!k.moveTo(nx - sx, ny - sy, why)) return false;
        // Where the part's centre is in the picture: the shot's middle away from the camera's centre.
        double partX = 0, partY = 0;
        if (!k.cal.pixelFor(k.cameraX - sx, k.cameraY - sy, k.cameraX, k.cameraY, partX, partY)) {
            why = "the camera's calibration cannot place the part";
            return false;
        }
        JPVisionPipelinePrep::shot(k.pipeline, k.composite, *shot, k.tip, partX, partY);
        cv::RotatedRect rect;
        if (!resultRect(k.pipeline, k.partId, rect, why)) return false;
        if (k.shown) k.shown(k.pipeline);
        // Its corners on the machine, from where the part's centre should be; each told apart (left or right,
        // upper or lower) in the part's own frame.
        cv::Point2f corners[4];
        rect.points(corners);
        std::array<JPVisionComposite::Point, 4> rel {};
        double mx = 0, my = 0;
        for (size_t i = 0; i < 4; ++i) {
            double x = 0, y = 0;
            if (!k.cal.machinePoint(corners[i].x, corners[i].y, k.cameraX, k.cameraY, x, y)) {
                why = "the camera's calibration cannot place the part";
                return false;
            }
            rel[i] = { x - (k.cameraX - sx), y - (k.cameraY - sy) };
            mx += rel[i].x / 4;
            my += rel[i].y / 4;
        }
        std::array<JPVisionComposite::Point, 4> points {};
        std::array<bool, 4> taken {};
        for (const auto& p : rel) {
            const double qx = c * (p.x - mx) + s * (p.y - my), qy = -s * (p.x - mx) + c * (p.y - my);
            const size_t idx = size_t((qx < 0 ? 0 : 1) + (qy > 0 ? 0 : 2));
            if (taken[idx]) {
                why = "ReferenceBottomVision (" + k.partId + "): the shot's rectangle is turned too far to tell its corners apart";
                return false;
            }
            taken[idx] = true;
            points[idx] = p;
        }
        k.composite.accumulate(*shot, points);
    }
    JPVisionComposite::Detected d;
    if (!k.composite.interpret(angle, d, why)) {
        why += " for part " + k.partId;
        return false;
    }
    // The part's centre with the nozzle where it is meant to be.
    seen.x = k.cameraX + d.center.x;
    seen.y = k.cameraY + d.center.y;
    seen.angle = d.angle;
    seen.widthMm = d.size.x;
    seen.heightMm = d.size.y;
    return true;
}

bool JPBottomVision::resultRect(JPPipeline& p, const std::string& partId, cv::RotatedRect& rect, std::string& why) {
    if (!p.process(why)) return false;
    // Its results ("result" in older pipelines): one rectangle.
    const JPPipeline::Result* r = p.result("results");
    if (!r) r = p.result("result");
    char buf[200];
    if (!r) {
        std::snprintf(buf, sizeof buf, "ReferenceBottomVision (%s): Pipeline error. Pipeline must contain a result named '%s'.",
                      partId.c_str(), "results");
        why = buf;
        return false;
    }
    if (const auto* f = r->model.failure()) {
        why = f->message;
        return false;
    }
    if (r->model.empty()) {
        std::snprintf(buf, sizeof buf, "ReferenceBottomVision (%s): No result found.", partId.c_str());
        why = buf;
        return false;
    }
    const auto* found = std::get_if<cv::RotatedRect>(&r->model.value);
    if (!found) {
        std::snprintf(buf, sizeof buf, "ReferenceBottomVision (%s): Incorrect pipeline result type (%s). Expected RotatedRect.",
                      partId.c_str(), r->model.kind().c_str());
        why = buf;
        return false;
    }
    rect = *found;
    return true;
}

} // inline namespace jf
