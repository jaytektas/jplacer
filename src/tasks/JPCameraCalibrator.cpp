// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPCameraCalibrator.h"

#include "JPCameraLook.h"

#include "common/JPLens.h"
#include "common/JPlacerLog.h"
#include "vision/JPCalibrationFit.h"
#include "vision/JPRoundMarkFinder.h"

#include <j/core/Log.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>

inline namespace jf {

namespace {

// Before the scale is known: how far from the picture's centre the first look
// searches, as a share of the picture's smaller side, and the size range tried.
constexpr double kFirstSearch  = 0.3;
constexpr double kMinMarkShare = 0.02;   // of the picture's smaller side
constexpr double kMaxMarkShare = 0.4;
// The first moves, small enough that the mark stays near where it was: a
// share of the picture's smaller side.
constexpr double kNudgeShare = 0.05;
// The grid's places across and down the picture come from the camera's
// settings (JPCameraConfig::Calibrating), at least this many each way: the
// lens is fitted with ten numbers, and its bending must show.
constexpr int kLeastGrid = 3;
// The small moves first made to find which way the mark goes.
constexpr size_t kDirectionMoves = 3;
// Samples before the lens is fitted for predicting where the next mark is.
constexpr size_t kLensPredictFrom = 8;
// Once three marks are measured, a mark is searched for this far from where
// the fit so far predicts, and accepted with this much of its edge round.
constexpr double kPredictedSearchPx = 25;
constexpr double kPredictedMinShape = 0.5;
// A find further from the fit than the settings' times its spread (and at
// least so many pixels) is left out; at most one in so many.
constexpr double kOutlierPx = 1.0;
constexpr size_t kMaxLeftOutShare = 10;
// At most one grid place in so many may go unmeasured.
constexpr size_t kMaxUnmeasuredShare = 5;
// How far the measured mark may be from the size it was said to be.
constexpr double kMarkSizeTolerance = 0.2;

std::string now() {
    const std::time_t t = std::time(nullptr);
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", std::localtime(&t));
    return buf;
}

} // namespace

std::optional<JPCameraCalibration> JPCameraCalibrator::run(JPCell& cell, JPCameraFeed& feed, const Options& o,
                                                           std::string& why, const Progress& progress) {
    // The offsets are kept as the camera's move relative to the mark, so a
    // camera that moves and one the mark moves over give one kind of
    // calibration: what is at a pixel, for a camera looking at a point.
    const JPMountConfig& mount = o.moving ? *o.moving : feed.config().mount;
    const double sign = o.moving ? -1 : 1;
    if (mount.axisX.empty() || mount.axisY.empty()) {
        why = o.moving ? "the tool carrying the mark does not move on X and Y"
                       : feed.config().name + " does not move with the head: calibrate it with a nozzle over it";
        return std::nullopt;
    }
    const JPCameraConfig& cam = feed.config();
    if (o.markDiameterMm <= 0 && (cam.unitsPerPixelX <= 0 || cam.unitsPerPixelY <= 0)) {
        why = "the mark's size, or the camera's rough scale, is needed";
        return std::nullopt;
    }
    if (!cell.isHomed()) {
        why = "home the machine first";
        return std::nullopt;
    }
    const auto start = cell.jogBase();
    const double x0 = start.at(mount.axisX), y0 = start.at(mount.axisY);
    JLOGC(JPlacerLog::kCamera, JLogLevel::Info) << feed.config().name << ": calibrating from " << x0 << ", " << y0;

    // First look: where is the mark, and how big?
    JPGrayImage img;
    if (!JPCameraLook::settled(feed, img, why)) return std::nullopt;
    const double side = std::min(img.width, img.height);
    const JPRoundMark first = JPRoundMarkFinder::findAnySize(img, img.width / 2.0, img.height / 2.0, kFirstSearch * side,
                                                             kMinMarkShare * side, kMaxMarkShare * side);
    if (!first.found) {
        why = feed.config().name + " sees no round mark near the middle of its picture: put it over one first ("
            + first.why + ")";
        return std::nullopt;
    }
    const double markPx = first.diameter;
    // The scale the mark's size suggests (else the camera's own rough one);
    // the moves measure the real one.
    const double guessPxPerMm = o.markDiameterMm > 0 ? markPx / o.markDiameterMm
                                                     : 2 / (cam.unitsPerPixelX + cam.unitsPerPixelY);

    std::vector<JPCalibrationFit::Sample> samples;
    auto back = [&] {
        std::string w;
        cell.moveAxesAndWait({ { mount.axisX, x0 }, { mount.axisY, y0 } }, o.speed, w);
    };
    int step = 0;
    // Moves the camera by (dx, dy) relative to the mark (the tool carrying
    // the mark the other way), finds the mark near where the samples so far
    // put it (around where it first was before there are three) and records it.
    int unmeasured = 0;   // grid places where the mark could not be measured (skipped)
    auto measure = [&](double dx, double dy, const char* phase, bool mayMiss) {
        ++step;
        if (progress) progress(std::string(phase) + ", move " + std::to_string(step));
        if (!cell.moveAxesAndWait({ { mount.axisX, x0 + sign * dx }, { mount.axisY, y0 + sign * dy } }, o.speed, why))
            return false;
        if (!JPCameraLook::settled(feed, img, why)) return false;
        double ex = first.x, ey = first.y, radius = kFirstSearch * side;
        const auto f = samples.size() >= kLensPredictFrom ? JPCalibrationFit::fitWithLens(samples, img.width, img.height, false)
                     : samples.size() >= 3                ? JPCalibrationFit::fit(samples)
                                                          : std::nullopt;
        if (f) {
            const JPLens lens = JPLens::forPicture(img.width, img.height, f->lensK1);
            lens.distort(f->centreX + f->pxPerMm[0] * dx + f->pxPerMm[1] * dy,
                         f->centreY + f->pxPerMm[2] * dx + f->pxPerMm[3] * dy, ex, ey);
            radius = kPredictedSearchPx;
        }
        JPRoundMarkFinder::Request rq;
        rq.expectedX = ex;
        rq.expectedY = ey;
        rq.searchRadius = radius;
        rq.diameter = markPx;
        // Where the fit says, within a few pixels, nothing else is mistaken
        // for it: the mark may be dimmer and bent towards the picture's
        // corners, and still be measured.
        if (radius == kPredictedSearchPx) rq.minShape = kPredictedMinShape;
        const JPRoundMark m = JPRoundMarkFinder::find(img, rq);
        if (!m.found) {
            // Towards the picture's corners the mark can be too dim and bent
            // to measure: a grid place may be skipped (a few, below).
            if (mayMiss) {
                ++unmeasured;
                JLOGC(JPlacerLog::kCamera, JLogLevel::Info) << "  offset " << dx << ", " << dy << ": not measured (" << m.why << ")";
                return true;
            }
            why = "lost the mark at move " + std::to_string(step) + ": " + m.why;
            return false;
        }
        samples.push_back({ dx, dy, m.x, m.y });
        JLOGC(JPlacerLog::kCamera, JLogLevel::Debug) << "  offset " << dx << ", " << dy << " -> " << m.x << ", " << m.y;
        return true;
    };

    // A rough fit from small moves, the mark staying near where it was...
    const double nudge = kNudgeShare * side / guessPxPerMm;
    for (const auto [dx, dy] : { std::pair{ 0.0, 0.0 }, { nudge, 0.0 }, { 0.0, nudge } })
        if (!measure(dx, dy, "finding the direction", false)) {
            back();
            return std::nullopt;
        }
    // ...says how a move carries the mark across the picture.
    const auto rough = JPCalibrationFit::fit(samples);
    const double det = rough ? rough->pxPerMm[0] * rough->pxPerMm[3] - rough->pxPerMm[1] * rough->pxPerMm[2] : 0;
    if (std::abs(det) < 1e-9) {
        why = "the mark did not move in the picture when the head moved: is it the right camera?";
        back();
        return std::nullopt;
    }
    // The grid: places across the whole picture, as near its edges as leaves
    // room for the mark and a search around it (the lens bends most there,
    // and a straightened picture's edges are only as good as the fit there),
    // each turned into a move by the rough fit; nearest the middle first, so
    // the lens is learned before the furthest are predicted.
    const JPCameraConfig::Calibrating& k = o.calibrating;
    const int columns = std::clamp(k.columns, kLeastGrid, JPCameraConfig::Calibrating::kMostPlaces);
    const int rows = std::clamp(k.rows, kLeastGrid, JPCameraConfig::Calibrating::kMostPlaces);
    const double reach = std::clamp(k.reach, 0.1, 1.0);
    const double reachX = (img.width / 2.0 - markPx - kPredictedSearchPx) * reach;
    const double reachY = (img.height / 2.0 - markPx - kPredictedSearchPx) * reach;
    struct Target { double dx, dy, r; };
    std::vector<Target> targets;
    for (int iy = 0; iy < rows; ++iy)
        for (int ix = 0; ix < columns; ++ix) {
            const double tx = reachX * (2.0 * ix / (columns - 1) - 1), ty = reachY * (2.0 * iy / (rows - 1) - 1);
            const double wantX = img.width / 2.0 + tx - rough->centreX, wantY = img.height / 2.0 + ty - rough->centreY;
            targets.push_back({ (rough->pxPerMm[3] * wantX - rough->pxPerMm[1] * wantY) / det,
                                (rough->pxPerMm[0] * wantY - rough->pxPerMm[2] * wantX) / det, std::hypot(tx, ty) });
        }
    std::stable_sort(targets.begin(), targets.end(), [](const Target& a, const Target& b) { return a.r < b.r; });
    for (const Target& t : targets)
        if (!measure(t.dx, t.dy, "measuring", true)) {
            back();
            return std::nullopt;
        }
    back();
    if (size_t(unmeasured) * kMaxUnmeasuredShare > targets.size()) {
        why = "the mark could not be measured at " + std::to_string(unmeasured) + " of " + std::to_string(targets.size())
            + " places: is the light on it, and is it the right mark?";
        return std::nullopt;
    }

    // The grid alone: the first small moves were for finding the way, and
    // the grid covers where they were.
    const std::vector<JPCalibrationFit::Sample> grid(samples.begin() + kDirectionMoves, samples.end());
    auto fit = JPCalibrationFit::fitWithLens(grid, img.width, img.height, true);
    if (!fit) {
        why = "the moves did not give a fit";
        return std::nullopt;
    }
    // A find far from the fit (a glint taken for the mark, a missed step) is
    // left out and the rest fitted again; a few at most, or it is not a fit.
    int leftOut = 0;
    const double limit = std::max(kOutlierPx, std::max(1.0, k.outlierSpread) * fit->rmsPx);
    {
        const std::vector<double> res = JPCalibrationFit::residualsPx(grid, *fit, img.width, img.height);
        std::vector<JPCalibrationFit::Sample> kept;
        for (size_t i = 0; i < grid.size(); ++i)
            if (res[i] <= limit) kept.push_back(grid[i]);
        leftOut = int(grid.size() - kept.size());
        if (leftOut > 0 && size_t(leftOut) <= grid.size() / kMaxLeftOutShare)
            if (const auto again = JPCalibrationFit::fitWithLens(kept, img.width, img.height, true)) fit = again;
        if (size_t(leftOut) > grid.size() / kMaxLeftOutShare) leftOut = 0;   // too many to leave out: the fit stands as it is, and is judged
    }
    if (fit->rmsPx > k.maxRmsPx) {
        char buf[160];
        std::snprintf(buf, sizeof buf, "the measurements disagree with each other by %.2f px (more than %.2f): "
                      "is the mark or the camera loose, or a drive missing steps?", fit->rmsPx, k.maxRmsPx);
        why = buf;
        return std::nullopt;
    }
    // The mark it measured must be the mark it was given: its size in mm,
    // through the fit, against the size expected.
    if (o.markDiameterMm > 0) {
        const double scale = std::sqrt(std::abs(fit->pxPerMm[0] * fit->pxPerMm[3] - fit->pxPerMm[1] * fit->pxPerMm[2]));
        const double measuredMm = markPx / scale;
        if (std::abs(measuredMm / o.markDiameterMm - 1) > kMarkSizeTolerance) {
            char buf[160];
            std::snprintf(buf, sizeof buf, "the mark measured %.2f mm across, not the %.2f mm expected: is it the right mark?",
                          measuredMm, o.markDiameterMm);
            why = buf;
            return std::nullopt;
        }
    }
    JPCameraCalibration c;
    c.valid   = true;
    c.pxPerMm = fit->pxPerMm;
    c.lensK1  = fit->lensK1;
    c.lensK2  = fit->lensK2;
    c.lensCentreX = fit->lensCentreX;
    c.lensCentreY = fit->lensCentreY;
    c.width   = img.width;
    c.height  = img.height;
    c.z       = o.markZ;
    c.rmsPx   = fit->rmsPx;
    c.leftOut = leftOut;
    c.unmeasured = unmeasured;
    c.when    = now();
    // Each measurement against the final fit, for the results' plots.
    c.outlierPx = limit;
    const auto res = JPCalibrationFit::residualVectorsPx(grid, *fit, img.width, img.height);
    for (size_t i = 0; i < grid.size(); ++i) {
        JPCameraCalibration::Point q;
        q.xPx = grid[i].xPx;
        q.yPx = grid[i].yPx;
        q.dxPx = res[i][0];
        q.dyPx = res[i][1];
        q.leftOut = leftOut > 0 && std::hypot(q.dxPx, q.dyPx) > limit;
        c.points.push_back(q);
    }
    JLOGC(JPlacerLog::kCamera, JLogLevel::Info) << feed.config().name << ": " << c.scaleX() << " x " << c.scaleY()
        << " px/mm, turned " << c.rotationDeg(cam.looksUp) << " deg" << (c.mirrored(cam.looksUp) ? ", mirrored" : "")
        << ", lens " << c.lensK1
        << ", fit " << c.rmsPx << " px";
    return c;
}

} // inline namespace jf
