// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPBlindsVision.h"

#include "common/JPlacerLog.h"
#include "model/JPBlindsFeeders.h"
#include "vision/JPSimpleHistogram.h"

#include <j/core/Log.h>

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>

inline namespace jf {

namespace {

constexpr JPLengthUnit kMm = JPLengthUnit::Millimeters;
// OpenPnP's: a fiducial's size and squareness; how much a blind may differ from its nominal size.
constexpr double kFidMinMm = 1.4, kFidMaxMm = 2.3, kFidAspect = 1.3, kTolerance = 1.4;
// Defaults before the pockets are known.
constexpr double kBlindMaxDefaultMm = 22, kBlindMaxAspect = 2, kBlindMinDefaultMm = 0.5, kBlindMinAspect = 3;
// How far from the camera a feature may be (mm, its share of the pocket size), and turned (degrees).
constexpr double kPositionToleranceMm = 5, kPocketSizeShare = 0.45, kAngleTolerance = 20, kPocketRangeFactor = 3.5,
                 kPocketRangeLeastMm = 2;
// A feature this close to the picture's edge is cut off (pixels).
constexpr double kEdgePx = 2;
// The histograms' resolutions (mm).
constexpr double kCornerResolution = 0.1, kPitchResolution = 2, kPositionResolution = 0.05;
// Lines drawn this long either way of their point (mm).
constexpr double kLineReachMm = 100;
// The drawing: rectangles 3 thick, lines 2, numbers no smaller than 0.6 mm, the OCR text big.
constexpr int    kRectThickness = 3, kLineThickness = 2;
constexpr double kMinFontSizeMm = 0.6, kOcrFontScale = 3;
constexpr int    kOcrMarginPx = 20;
// OpenPnP's colours (BGR): blinds blue (centres yellow), fiducials white, lines navy, numbers and text orange.
const cv::Scalar kBlue(255, 0, 0), kYellow(0, 255, 255), kWhite(255, 255, 255), kNavy(128, 0, 0), kOrange(0, 200, 255), kBlack(0, 0, 0);

double angleNorm(double a) {
    while (a > 180) a -= 360;
    while (a <= -180) a += 360;
    return a;
}

double mm(const JPLength& l) { return l.convertToUnits(kMm).value(); }

} // namespace

bool JPBlindsVision::find(const JPFeeder& f, const JPPipeline& pipeline, const JPJobMachine::Sight& camera, Found& found, std::string& why) {
    found = {};
    const JPPipeline::Result* r = pipeline.result("results");
    std::vector<cv::RotatedRect> results;
    if (r) {
        if (const auto* l = std::get_if<std::vector<cv::RotatedRect>>(&r->model.value)) results = *l;
        else if (const auto* fail = r->model.failure()) {
            why = fail->message;
            return false;
        } else if (!r->model.empty()) {
            why = "Unrecognized result type (should be RotatedRect): " + r->model.kind();
            return false;
        }
    }
    // Text read, when OCR was on.
    if (const JPPipeline::Result* o = pipeline.result("OCR"))
        if (const JPPipelineValue* a = pipeline.property("SimpleOcr.alphabet"))
            if (const auto* text = std::get_if<std::string>(&a->value); text && !text->empty())
                if (const auto* ocr = std::get_if<JPPipelineModel::Ocr>(&o->model.value)) {
                    found.ocrText = ocr->text;
                    found.ocrAvgScore = ocr->numChars > 0 ? ocr->overallScore / ocr->numChars : 0;
                }
    find(f, results, camera, found);
    return true;
}

void JPBlindsVision::find(const JPFeeder& f, const std::vector<cv::RotatedRect>& results, const JPJobMachine::Sight& camera, Found& found) {
    const double scaleX = camera.mmPerPixelX, scaleY = camera.mmPerPixelY;
    const double w = mm(f.lengthOf("pocket-pitch", JPLength(0, kMm))) * 0.5, h = mm(f.lengthOf("pocket-size", JPLength(0, kMm)));
    double blindMin = std::min(w, h) / kTolerance, blindMax = std::max(w, h) * kTolerance;
    double blindAspect = kTolerance * kTolerance * blindMax / blindMin;
    if (blindMax == 0) {
        blindMax = kBlindMaxDefaultMm;
        blindAspect = kBlindMaxAspect;
    }
    if (blindMin == 0) {
        blindMin = kBlindMinDefaultMm;
        blindAspect = kBlindMinAspect;
    }
    double positionTolerance = kPositionToleranceMm;
    if (h > 0) positionTolerance = h * kPocketSizeShare;
    const double pocketRange = std::max(w, kPocketRangeLeastMm) * kPocketRangeFactor;
    const JPLocation cameraFeeder = JPBlindsFeeders::machineToFeeder(f, camera.at);
    // Before fiducials 1 and 2: anything goes.
    const bool tolerant = !(f.locationOf("fiducial-1-location").isInitialized() && f.locationOf("fiducial-2-location").isInitialized());
    JPSimpleHistogram upper(kCornerResolution), lower(kCornerResolution);
    auto feederAt = [&](double px, double py) { return JPBlindsFeeders::machineToFeeder(f, camera.toMachine(px, py)); };
    for (const cv::RotatedRect& rect : results) {
        cv::Point2f points[4];
        rect.points(points);
        // Cut off by the picture's edge (OpenPnP looks at every other corner).
        bool atMargin = false;
        for (int i = 0; i < 4; i += 2)
            if (points[i].x <= kEdgePx || points[i].x >= camera.width - kEdgePx || points[i].y <= kEdgePx || points[i].y >= camera.height - kEdgePx)
                atMargin = true;
        if (atMargin) continue;
        const JPLocation center = feederAt(rect.center.x, rect.center.y);
        const double angle = JPBlindsFeeders::pixelToFeederAngle(f, rect.angle);
        const double sx = scaleX * rect.size.width, sy = scaleY * rect.size.height;
        if ((tolerant || cameraFeeder.linearDistanceTo(center) < positionTolerance)
            && (tolerant || std::abs(angleNorm(angle - 45)) < kAngleTolerance) && sx > kFidMinMm && sx < kFidMaxMm && sy > kFidMinMm
            && sy < kFidMaxMm && sx / sy < kFidAspect && sy / sx < kFidAspect)
            found.fiducials.push_back(rect);
        if ((tolerant || std::abs(cameraFeeder.y() - center.y()) < positionTolerance)
            && (tolerant || std::abs(cameraFeeder.x() - center.x()) < pocketRange) && (tolerant || std::abs(angleNorm(angle)) < kAngleTolerance)
            && sx > blindMin && sx < blindMax && sy > blindMin && sy < blindMax && sx / sy < blindAspect && sy / sx < blindAspect) {
            // Its corners across the tape, for the pocket size.
            if (!tolerant)
                for (const cv::Point2f& p : points) {
                    const JPLocation corner = feederAt(p.x, p.y);
                    (corner.y() < cameraFeeder.y() ? lower : upper).add(corner.y(), 1.);
                }
            found.blinds.push_back(rect);
        }
    }
    // Fiducials nearest the camera first; blinds along the tape.
    auto fromCamera = [&](const cv::RotatedRect& a) { return camera.toMachine(a.center.x, a.center.y).linearDistanceTo(camera.at); };
    std::stable_sort(found.fiducials.begin(), found.fiducials.end(),
                     [&](const cv::RotatedRect& a, const cv::RotatedRect& b) { return fromCamera(a) < fromCamera(b); });
    std::stable_sort(found.blinds.begin(), found.blinds.end(), [&](const cv::RotatedRect& a, const cv::RotatedRect& b) {
        return feederAt(a.center.x, a.center.y).x() < feederAt(b.center.x, b.center.y).x();
    });
    // The pocket size and centerline from the corners' commonest Y either side.
    const double bestLower = lower.maximumKey(), bestUpper = upper.maximumKey();
    found.pocketSizeMm = bestUpper - bestLower;
    // As Java's Math.round: halves up, and none (NaN) 0.
    const double middle = (bestUpper + bestLower) * 0.5;
    found.pocketCenterlineMm = std::isnan(middle) ? 0 : std::floor(middle + 0.5);
    auto lineAcross = [&](const JPLocation& a, const JPLocation& b) {
        double ax = 0, ay = 0, bx = 0, by = 0;
        if (camera.toPixel(JPBlindsFeeders::feederToMachine(f, a), ax, ay) && camera.toPixel(JPBlindsFeeders::feederToMachine(f, b), bx, by))
            found.lines.push_back({ { ax, ay }, { bx, by } });
    };
    for (const double y : { bestLower, found.pocketCenterlineMm, bestUpper })
        if (!std::isnan(y)) lineAcross(JPLocation(kMm, -kLineReachMm, y, 0, 0), JPLocation(kMm, kLineReachMm, y, 0, 0));
    // The pocket pitch from the blinds' commonest distance.
    JPSimpleHistogram pitch(kPitchResolution);
    std::optional<JPLocation> previous;
    for (const cv::RotatedRect& rect : found.blinds) {
        const JPLocation l = feederAt(rect.center.x, rect.center.y);
        if (previous) pitch.add(l.x() - previous->x(), 1.0);
        previous = l;
    }
    found.pocketPitchMm = pitch.maximumKey();
    // The pocket position from the tape's start: the blinds near the camera, modulo the pitch.
    const double setPitch = mm(f.lengthOf("pocket-pitch", JPLength(0, kMm)));
    const double pitchForPosition = setPitch != 0. ? setPitch : found.pocketPitchMm;
    JPSimpleHistogram distance(kPositionResolution);
    for (const cv::RotatedRect& rect : found.blinds) {
        const JPLocation l = feederAt(rect.center.x, rect.center.y);
        if (std::abs(l.x() - cameraFeeder.x()) < (2 + pitchForPosition * 1.5)) {
            double position = std::fmod(l.x(), pitchForPosition);
            if (position < 0.) position += pitchForPosition;
            // Three bins a pitch apart, so the kernel wraps round.
            distance.add(position - pitchForPosition, 1.0);
            distance.add(position, 1.0);
            distance.add(position + pitchForPosition, 1.0);
        }
    }
    found.pocketPositionMm = distance.maximumKey();
    if (!std::isnan(found.pocketPositionMm)) {
        if (found.pocketPositionMm < 0.0) found.pocketPositionMm += found.pocketPitchMm;
        if (found.pocketPositionMm > found.pocketPitchMm) found.pocketPositionMm -= found.pocketPitchMm;
        lineAcross(JPLocation(kMm, found.pocketPositionMm, found.pocketCenterlineMm - kLineReachMm, 0, 0),
                   JPLocation(kMm, found.pocketPositionMm, found.pocketCenterlineMm + kLineReachMm, 0, 0));
    }
    JLOGC(JPlacerLog::kJob, JLogLevel::Debug) << "blinds: pocket centerline " << found.pocketCenterlineMm << "mm, size "
                                              << found.pocketSizeMm << "mm, pitch " << found.pocketPitchMm << "mm, position "
                                              << found.pocketPositionMm << "mm";
}

void JPBlindsVision::draw(cv::Mat& bgr, const JPFeeder& f, const Found& found, const JPJobMachine::Sight& camera) {
    auto rects = [&](const std::vector<cv::RotatedRect>& list, const cv::Scalar& color, const cv::Scalar& center) {
        for (const cv::RotatedRect& r : list) {
            cv::Point2f p[4];
            r.points(p);
            for (int i = 0; i < 4; ++i) cv::line(bgr, p[i], p[(i + 1) % 4], color, kRectThickness);
            cv::circle(bgr, r.center, 2, center, 3, cv::LINE_AA);
        }
    };
    rects(found.blinds, kBlue, kYellow);
    rects(found.fiducials, kWhite, kWhite);
    for (const JPRansac::Line& l : found.lines) cv::line(bgr, cv::Point2d(l.a.x, l.a.y), cv::Point2d(l.b.x, l.b.y), kNavy, kLineThickness);
    // The pockets numbered below them, no denser than they can be read.
    const double pitchMm = mm(f.lengthOf("pocket-pitch", JPLength(0, kMm)));
    const int count = f.number("pocket-count", 0);
    if (pitchMm >= 1. && camera.mmPerPixelX > 0) {
        const double sizeMm = mm(f.lengthOf("pocket-size", JPLength(0, kMm)));
        double fontScale = 1.0;
        int baseLine = 0;
        const cv::Size size = cv::getTextSize(std::to_string(count), cv::FONT_HERSHEY_PLAIN, fontScale, 2, &baseLine);
        double textW = size.width * camera.mmPerPixelX, textH = std::abs(size.height * camera.mmPerPixelY);
        if (textH < kMinFontSizeMm) {
            fontScale = kMinFontSizeMm / textH;
            textW *= fontScale;
            textH *= fontScale;
        }
        const double pitches = std::hypot(textW, textH) / pitchMm;
        const int step = pitches < 0.75 ? 1 : pitches < 1.5 ? 2 : pitches < 4 ? 5 : 0;
        for (int i = step; step > 0 && i <= count; i += step) {
            const std::string text = std::to_string(i);
            const cv::Size ts = cv::getTextSize(text, cv::FONT_HERSHEY_PLAIN, fontScale, 2, &baseLine);
            const JPLocation part = JPBlindsFeeders::pickLocation(f, i);
            JPLocation where = JPBlindsFeeders::machineToFeeder(f, part).add(JPLocation(kMm, 0., -sizeMm * 0.5 - textH * 0.25, 0., 0.));
            where = JPBlindsFeeders::feederToMachine(f, where);
            double px = 0, py = 0;
            if (!camera.toPixel(where, px, py) || px <= 0 || px >= camera.width || py <= 0 || py >= camera.height) continue;
            const double dx = where.x() - part.x(), dy = where.y() - part.y();
            double alignX = 0, alignY = 0;
            if (std::abs(dx) > std::abs(dy)) {
                alignX = dx < 0 ? -ts.width : 0.;
                alignY = ts.height / 2.0;
            } else {
                alignX = -ts.width / 2.0;
                alignY = dy > 0 ? 0.0 : ts.height;
            }
            cv::putText(bgr, text, cv::Point2d(px + alignX, py + alignY), cv::FONT_HERSHEY_PLAIN, fontScale, kOrange, 2);
        }
    }
    if (found.ocrText) {
        const cv::Point at(kOcrMarginPx, bgr.rows - kOcrMarginPx);
        cv::putText(bgr, *found.ocrText, at, cv::FONT_HERSHEY_PLAIN, kOcrFontScale, kBlack, 6);
        cv::putText(bgr, *found.ocrText, at, cv::FONT_HERSHEY_PLAIN, kOcrFontScale, kOrange, 2);
    }
}

} // inline namespace jf
