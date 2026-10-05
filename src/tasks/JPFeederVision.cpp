// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPFeederVision.h"

#include "common/JPlacerLog.h"

#include <j/core/Log.h>

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cfloat>
#include <cmath>

inline namespace jf {

namespace {

constexpr JPLengthUnit kMm = JPLengthUnit::Millimeters;
// EIA-481's: the sprocket holes' size and pitch; the least distance from a part to its holes' line.
constexpr double kHoleDiameterMm = 1.5, kHolePitchMm = 4, kMinHolesDistanceMm = 3.5;
// EIA-481's grid of pick locations: along the tape, 2 mm; across, 3.5 mm from the holes (8 mm tape), and 2 mm more for each wider tape.
constexpr double kPartPitchMinMm = 2, kHoleToPartMinMm = 3.5, kHoleToPartGridMm = 2;
// A tape's lines are drawn this thick, its numbers no smaller than this (mm), its OCR text this big.
constexpr int    kLineThickness = 2;
constexpr double kMinFontSizeMm = 0.6, kOcrFontScale = 3;
constexpr int    kOcrMarginPx = 20;
// OpenPnP's colours (BGR): holes and numbers green, lines blue, the OCR text orange, nothing found red.
const cv::Scalar kGreen(0, 255, 0, 255), kBlue(255, 0, 0, 255), kOrange(0, 200, 255, 255), kRed(0, 0, 255, 255),
    kBlack(0, 0, 0, 255);

double angleNorm180(double a) {
    while (a > 180) a -= 360;
    while (a <= -180) a += 360;
    return a;
}

double dot(const JPLocation& a, const JPLocation& b) {
    const JPLocation bb = b.convertToUnits(a.units());
    return a.x() * bb.x() + a.y() * bb.y() + a.z() * bb.z();
}

} // namespace

bool JPFeederVision::find(const JPPipelineModel& results, Mode mode, const Settings& settings, const Camera& camera, Found& found,
                          std::string& why) {
    found = {};
    const JPFeederTape::Params& tape = settings.tape;
    const JPLocation partLocation = tape.partLocation, hole1Location = tape.hole1Location, hole2Location = tape.hole2Location;
    if (mode == Mode::OcrOnly) {
        // No vision calibration wanted: the locations as they are set.
        found.hole1 = hole1Location;
        found.hole2 = hole2Location;
        found.pick = partLocation;
        return true;
    }
    const double tolMm = settings.sprocketHoleToleranceMm, scale = camera.mmPerPixelX;
    const double holeDiameterPx = kHoleDiameterMm / scale, holePitchPx = kHolePitchMm / scale, tolPx = tolMm / scale;

    // The eligible results as circles.
    std::vector<Circle> circles;
    if (const auto* l = std::get_if<std::vector<JPPipelineModel::Circle>>(&results.value)) {
        for (const JPPipelineModel::Circle& c : *l) {
            if (std::abs(c.diameter * scale - kHoleDiameterMm) < tolMm) circles.push_back({ c.x, c.y, c.diameter });
            else
                JLOGC(JPlacerLog::kJob, JLogLevel::Debug) << "Dismissed Circle with non-compliant diameter " << c.diameter * scale
                                                          << "mm, allowed tolerance is ±" << tolMm << "mm";
        }
    } else if (const auto* l = std::get_if<std::vector<cv::RotatedRect>>(&results.value)) {
        for (const cv::RotatedRect& r : *l) {
            if (std::abs(r.size.width * scale - kHoleDiameterMm) < tolMm && std::abs(r.size.height * scale - kHoleDiameterMm) < tolMm)
                circles.push_back({ r.center.x, r.center.y, (r.size.width + r.size.height) / 2.0 });
            else
                JLOGC(JPlacerLog::kJob, JLogLevel::Debug)
                    << "Dismissed RotatedRect with non-compliant width or height " << r.size.width * scale << "mm x "
                    << r.size.height * scale << "mm, allowed tolerance is ±" << tolMm << "mm";
        }
    } else if (const auto* l = std::get_if<std::vector<cv::KeyPoint>>(&results.value)) {
        for (const cv::KeyPoint& k : *l) circles.push_back({ k.pt.x, k.pt.y, holeDiameterPx });
    } else if (const auto* f = results.failure()) {
        why = f->message;
        return false;
    } else if (!results.empty()) {
        why = "Unrecognized result type (should be Result.Circle, RotatedRect, KeyPoint): " + results.kind();
        return false;
    }

    std::vector<JPRansac::Point> points;
    for (const Circle& c : circles) points.push_back({ c.x, c.y });
    const std::vector<JPRansac::Line> lines = JPRansac::lines(points, 100, tolPx, holePitchPx, tolPx, false);
    if (lines.empty())
        JLOGC(JPlacerLog::kJob, JLogLevel::Debug) << "Ransac algorithm has not found any lines of sprocket holes with " << kHolePitchMm
                                                  << "mm pitch, allowed pitch and line tolerance is ±" << tolMm << "mm";
    // The best line within the calibration tolerance.
    const JPLocation cameraAt = camera.at.convertToUnits(kMm);
    std::optional<JPRansac::Line> best;
    JPLocation bestUnit(kMm);
    double bestDistanceMm = DBL_MAX;
    for (const JPRansac::Line& line : lines) {
        const JPLocation a = camera.toMachine(line.a.x, line.a.y), b = camera.toMachine(line.b.x, line.b.y);
        // From a part, a least distance not to take its pocket for holes, then the nearest (not the next
        // tape's); calibrating from between the holes, the first within the calibration tolerance.
        const double distanceMm = JPFeederTape::distanceToSegment(cameraAt, a, b);
        const double minDistanceMm = (mode == Mode::CalibrateHoles ? 0 : kMinHolesDistanceMm) - tolMm;
        const double maxDistanceMm = mode == Mode::CalibrateHoles ? settings.calibrationToleranceMm : bestDistanceMm;
        JLOGC(JPlacerLog::kJob, JLogLevel::Debug) << "Found sprocket holes line with distance " << distanceMm << "mm [min: "
                                                  << minDistanceMm << "mm, max: " << maxDistanceMm << "mm]";
        if (distanceMm >= minDistanceMm && distanceMm < maxDistanceMm) {
            best = line;
            bestUnit = JPFeederTape::unitVector(a, b);
            bestDistanceMm = distanceMm;
            found.lines.push_back(line);
            if (mode == Mode::CalibrateHoles) break;
        } else if (mode == Mode::Preview) {
            found.lines.push_back(line);
            JLOGC(JPlacerLog::kJob, JLogLevel::Debug) << "Dismissed line by distance, " << distanceMm << "mm, not within "
                                                      << minDistanceMm << "mm .. " << maxDistanceMm << "mm";
        }
    }
    if (mode != Mode::Preview && !best) {
        why = "No line of sprocket holes can be recognized";
        return false;
    }
    if (!best) return true;

    // The circles on it, nearest the camera first.
    for (const Circle& c : circles)
        if (JPRansac::pointToLineDistance(best->a, best->b, { c.x, c.y }) <= tolPx) found.holes.push_back(c);
    auto fromCamera = [&](const Circle& c) { return camera.toMachine(c.x, c.y).linearDistanceTo(cameraAt); };
    std::stable_sort(found.holes.begin(), found.holes.end(),
                     [&](const Circle& a, const Circle& b) { return fromCamera(a) < fromCamera(b); });

    if (mode == Mode::FromPickLocationGetHoles
        || (mode == Mode::Preview && !(hole1Location.isInitialized() && hole2Location.isInitialized()))) {
        // The nearest two are holes 1 and 2.
        if (found.holes.size() < 2) {
            why = "At least two sprocket holes need to be recognized";
            return false;
        }
        JPLocation h1 = camera.toMachine(found.holes[0].x, found.holes[0].y).convertToUnits(kMm);
        JPLocation h2 = camera.toMachine(found.holes[1].x, found.holes[1].y).convertToUnits(kMm);
        const double angle1 = std::atan2(h1.y() - cameraAt.y(), h1.x() - cameraAt.x());
        const double angle2 = std::atan2(h2.y() - cameraAt.y(), h2.x() - cameraAt.x());
        // Holes 1 and 2 counter-clockwise from the part: swapped.
        if (angleNorm180((angle2 - angle1) * 180 / M_PI) > 0) std::swap(h1, h2);
        if (dot(JPFeederTape::unitVector(h1, h2), bestUnit) < 0.0) bestUnit = bestUnit.multiply(-1.0, -1.0, 0, 0);
        const double angleTape = std::atan2(bestUnit.y(), bestUnit.x()) * 180.0 / M_PI;
        found.hole1 = h1;
        found.hole2 = h2;
        // The preliminary pick location: the camera's, at the old Z, turned with the tape.
        found.pick = camera.at.derive(partLocation, false, false, true, false)
                         .derive(std::nullopt, std::nullopt, std::nullopt, angleTape);
    } else {
        // The two holes matching those set.
        std::optional<JPLocation> h1, h2;
        for (const Circle& hole : found.holes) {
            const JPLocation l = camera.toMachine(hole.x, hole.y).convertToUnits(kMm);
            const double d1 = l.linearDistanceTo(hole1Location), d2 = l.linearDistanceTo(hole2Location);
            if (d1 < settings.calibrationToleranceMm && d1 < d2) h1 = l;
            else if (d2 < settings.calibrationToleranceMm && d2 < d1) h2 = l;
        }
        if (!h1 || !h2) {
            if (mode == Mode::CalibrateHoles) {
                why = "The two reference sprocket holes cannot be recognized";
                return false;
            }
        } else {
            if (dot(JPFeederTape::unitVector(*h1, *h2), bestUnit) < 0.0) bestUnit = bestUnit.multiply(-1.0, -1.0, 0, 0);
            if (settings.snapToAxis) {
                if (std::abs(bestUnit.x()) > std::abs(bestUnit.y()) * 5)
                    bestUnit = JPLocation(kMm, bestUnit.x() > 0 ? 1 : -1, 0, 0, 0);
                else if (std::abs(bestUnit.y()) > std::abs(bestUnit.x()) * 5)
                    bestUnit = JPLocation(kMm, 0, bestUnit.y() > 0 ? 1 : -1, 0, 0);
            }
            const double angleTape = std::atan2(bestUnit.y(), bestUnit.x()) * 180.0 / M_PI;
            // About their mid-point, a whole hole pitch apart (undistorted by the lens and Z parallax).
            const JPLocation mid = h1->add(*h2).multiply(0.5, 0.5, 0, 0);
            const double holesMm = std::round(h1->linearDistanceTo(*h2) / kHolePitchMm) * kHolePitchMm;
            found.hole1 = mid.subtract(bestUnit.multiply(holesMm * 0.5, holesMm * 0.5, 0, 0));
            found.hole2 = mid.add(bestUnit.multiply(holesMm * 0.5, holesMm * 0.5, 0, 0));
            JLOGC(JPlacerLog::kJob, JLogLevel::Trace) << "[FeederVisionHelper] calibrated hole locations are: "
                                                      << found.hole1->text() << ", " << found.hole2->text();
            if (mode == Mode::CalibrateHoles) {
                // The pick location from hole 1, turned from the old angle.
                const JPLocation pick = partLocation.convertToUnits(kMm);
                JPLocation relative = pick.subtract(hole1Location).rotateXy(-pick.rotation())
                                          .derive(std::nullopt, std::nullopt, std::nullopt, 0.0);
                // Onto EIA-481's grid.
                if (settings.normalizePickLocation)
                    relative = JPLocation(kMm, std::floor(relative.x() / kPartPitchMinMm + 0.5) * kPartPitchMinMm,
                                          -kHoleToPartMinMm
                                              + std::floor((relative.y() + kHoleToPartMinMm) / kHoleToPartGridMm + 0.5) * kHoleToPartGridMm,
                                          0, 0);
                found.pick = found.hole1->add(relative.rotateXy(angleTape)).derive(std::nullopt, std::nullopt, pick.z(), angleTape);
            }
        }
    }

    if (found.hole1 && found.pick) {
        // The vision offset (its Z always 0).
        found.visionOffset = partLocation.subtractWithRotation(*found.pick).derive(std::nullopt, std::nullopt, 0.0, std::nullopt);
        JLOGC(JPlacerLog::kJob, JLogLevel::Debug) << "calibrated vision offset is: " << found.visionOffset->text() << ", length is: "
                                                  << found.visionOffset->linearDistanceTo(JPLocation::origin());
        // Tick marks across and along the tape at the pick location.
        const JPLocation tick(kMm, -bestUnit.y(), bestUnit.x(), 0, 0);
        for (const auto& [from, to] : { std::pair { found.pick->subtract(tick), found.pick->add(tick) },
                                        std::pair { found.pick->subtract(bestUnit), found.pick->add(bestUnit) } }) {
            double ax = 0, ay = 0, bx = 0, by = 0;
            if (camera.toPixel(from, ax, ay) && camera.toPixel(to, bx, by)) found.lines.push_back({ { ax, ay }, { bx, by } });
        }
        JLOGC(JPlacerLog::kJob, JLogLevel::Debug) << "calibrated pick location is: " << found.pick->text();
    }
    return true;
}

void JPFeederVision::draw(cv::Mat& bgr, const Found& found, const Settings& settings, const Camera& camera) {
    for (const Circle& c : found.holes) {
        const cv::Point2d p(c.x, c.y);
        cv::circle(bgr, p, int(c.diameter + 0.5) / 2, kGreen, kLineThickness, cv::LINE_AA);
        cv::circle(bgr, p, 1, kGreen, 3, cv::LINE_AA);
    }
    for (const JPRansac::Line& l : found.lines)
        cv::line(bgr, cv::Point2d(l.a.x, l.a.y), cv::Point2d(l.b.x, l.b.y), kBlue, kLineThickness, cv::LINE_AA);

    // The parts' numbers in their pockets, no denser than they can be read.
    const double pitchMm = settings.tape.partPitch.convertToUnits(kMm).value();
    const long cycle = JPFeederTape::partsPerFeedCycle(settings.tape);
    if (pitchMm >= 1. && camera.mmPerPixelX > 0) {
        double fontScale = 1.0;
        int baseLine = 0;
        const cv::Size size = cv::getTextSize(std::to_string(cycle), cv::FONT_HERSHEY_PLAIN, fontScale, 2, &baseLine);
        double textW = size.width * camera.mmPerPixelX, textH = std::abs(size.height * camera.mmPerPixelY);
        if (textH < kMinFontSizeMm) {
            fontScale = kMinFontSizeMm / textH;
            textW *= fontScale;
            textH *= fontScale;
        }
        const double textPitches = std::hypot(textW, textH) / pitchMm;
        const int step = textPitches < 0.75 ? 1 : textPitches < 1.5 ? 2 : textPitches < 4 ? 5 : 0;
        for (long i = step; step > 0 && i <= cycle; i += step) {
            const std::string text = std::to_string(i);
            const cv::Size ts = cv::getTextSize(text, cv::FONT_HERSHEY_PLAIN, fontScale, 2, &baseLine);
            const JPLocation part = JPFeederTape::partLocation(i, found.visionOffset, settings.tape, 0).convertToUnits(kMm);
            JPLocation where = JPFeederTape::machineToFeeder(part, found.visionOffset, settings.tape);
            where = where.add(JPLocation(kMm, 0., -textH * 0.25, 0., 0.));
            where = JPFeederTape::feederToMachine(where, found.visionOffset, settings.tape).convertToUnits(kMm);
            double px = 0, py = 0;
            if (!camera.toPixel(where, px, py) || px <= 0 || px >= camera.width || py <= 0 || py >= camera.height) continue;
            // Aligned by where the text is from its pocket.
            const double dx = where.x() - part.x(), dy = where.y() - part.y();
            double alignX = 0, alignY = 0;
            if (std::abs(dx) > std::abs(dy)) {
                alignX = dx < 0 ? -ts.width : 0.;
                alignY = ts.height / 2.0;
            } else {
                alignX = -ts.width / 2.0;
                alignY = dy > 0 ? 0.0 : ts.height;
            }
            cv::putText(bgr, text, cv::Point2d(px + alignX, py + alignY), cv::FONT_HERSHEY_PLAIN, fontScale, kGreen, 2);
        }
    }
    if (found.ocrText) {
        const cv::Point at(kOcrMarginPx, bgr.rows - kOcrMarginPx);
        cv::putText(bgr, *found.ocrText, at, cv::FONT_HERSHEY_PLAIN, kOcrFontScale, kBlack, 6);
        cv::putText(bgr, *found.ocrText, at, cv::FONT_HERSHEY_PLAIN, kOcrFontScale, kOrange, 2);
    }
    if (found.holes.empty()) {
        cv::line(bgr, cv::Point(0, 0), cv::Point(bgr.cols - 1, bgr.rows - 1), kRed, kLineThickness, cv::LINE_AA);
        cv::line(bgr, cv::Point(0, bgr.rows - 1), cv::Point(bgr.cols - 1, 0), kRed, kLineThickness, cv::LINE_AA);
    }
}

} // inline namespace jf
