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
// The first moves, small enough that the mark stays near where it was, and
// the grid's reach from the picture's middle: shares of its smaller side.
constexpr double kNudgeShare = 0.05;
constexpr double kGridShare  = 0.4;
// The grid's points along each side (odd: one through the middle). Enough
// across the picture for the lens's bending to show and be fitted.
constexpr int kGridSide = 5;
// The small moves first made to find which way the mark goes.
constexpr size_t kDirectionMoves = 3;
// Samples before the lens is fitted for predicting where the next mark is.
constexpr size_t kLensPredictFrom = 8;
// Once three marks are measured, a mark is searched for this far from where
// the fit so far predicts, and accepted with this much of its edge round.
constexpr double kPredictedSearchPx = 25;
constexpr double kPredictedMinShape = 0.5;
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
    const JPMountConfig& mount = feed.config().mount;
    if (mount.axisX.empty() || mount.axisY.empty()) {
        why = feed.config().name + " does not move with the head: it is calibrated another way";
        return std::nullopt;
    }
    if (o.markDiameterMm <= 0) {
        why = "the mark's size is needed";
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
    // The scale the mark's size suggests; the moves measure the real one.
    const double guessPxPerMm = markPx / o.markDiameterMm;

    std::vector<JPCalibrationFit::Sample> samples;
    auto back = [&] {
        std::string w;
        cell.moveAxesAndWait({ { mount.axisX, x0 }, { mount.axisY, y0 } }, o.speed, w);
    };
    int step = 0;
    // Moves to (x0 + dx, y0 + dy), finds the mark near where the samples so far
    // put it (around where it first was before there are three) and records it.
    auto measure = [&](double dx, double dy, const char* phase) {
        ++step;
        if (progress) progress(std::string(phase) + ", move " + std::to_string(step));
        if (!cell.moveAxesAndWait({ { mount.axisX, x0 + dx }, { mount.axisY, y0 + dy } }, o.speed, why)) return false;
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
        if (!measure(dx, dy, "finding the direction")) {
            back();
            return std::nullopt;
        }
    // ...says where the mark is in the middle of the picture: the grid is laid
    // around there, reaching well across the picture so the scale is measured
    // over many pixels rather than few.
    const auto rough = JPCalibrationFit::fit(samples);
    const double det = rough ? rough->pxPerMm[0] * rough->pxPerMm[3] - rough->pxPerMm[1] * rough->pxPerMm[2] : 0;
    if (std::abs(det) < 1e-9) {
        why = "the mark did not move in the picture when the head moved: is the camera on the head?";
        back();
        return std::nullopt;
    }
    const double wantX = img.width / 2.0 - rough->centreX, wantY = img.height / 2.0 - rough->centreY;
    const double cx = (rough->pxPerMm[3] * wantX - rough->pxPerMm[1] * wantY) / det;
    const double cy = (rough->pxPerMm[0] * wantY - rough->pxPerMm[2] * wantX) / det;
    const double spacing = kGridShare * side / std::sqrt(std::abs(det)) / (kGridSide / 2);
    for (int iy = -(kGridSide / 2); iy <= kGridSide / 2; ++iy)
        for (int ix = -(kGridSide / 2); ix <= kGridSide / 2; ++ix)
            if (!measure(cx + ix * spacing, cy + iy * spacing, "measuring")) {
                back();
                return std::nullopt;
            }
    back();

    // The grid alone: the first small moves were for finding the way, and
    // the grid covers where they were.
    const std::vector<JPCalibrationFit::Sample> grid(samples.begin() + kDirectionMoves, samples.end());
    const auto fit = JPCalibrationFit::fitWithLens(grid, img.width, img.height, true);
    if (!fit) {
        why = "the moves did not give a fit";
        return std::nullopt;
    }
    if (fit->rmsPx > o.maxRmsPx) {
        char buf[160];
        std::snprintf(buf, sizeof buf, "the measurements disagree with each other by %.2f px (more than %.2f): "
                      "is the mark or the camera loose, or a drive missing steps?", fit->rmsPx, o.maxRmsPx);
        why = buf;
        return std::nullopt;
    }
    // The mark it measured must be the mark it was given: its size in mm,
    // through the fit, against the size expected.
    {
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
    c.lensCentreX = fit->lensCentreX;
    c.lensCentreY = fit->lensCentreY;
    c.width   = img.width;
    c.height  = img.height;
    c.z       = o.markZ;
    c.rmsPx   = fit->rmsPx;
    c.when    = now();
    JLOGC(JPlacerLog::kCamera, JLogLevel::Info) << feed.config().name << ": " << c.scaleX() << " x " << c.scaleY()
        << " px/mm, turned " << c.rotationDeg() << " deg" << (c.mirrored() ? ", mirrored" : "") << ", lens " << c.lensK1
        << ", fit " << c.rmsPx << " px";
    return c;
}

} // inline namespace jf
