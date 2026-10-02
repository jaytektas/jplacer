// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPBoardLocator.h"

#include "JPCameraLook.h"

#include "common/JPlacerLog.h"
#include "geometry/JPPointFit.h"
#include "vision/JPRoundMarkFinder.h"

#include <j/core/Log.h>

#include <cmath>
#include <cstdio>

inline namespace jf {

namespace {

// A fiducial is measured once it is this near the middle of the picture
// (mm), after at most so many moves to centre it.
constexpr double kCentredMm = 0.01;
constexpr int    kCentringMoves = 4;
// Once the camera is over it, how far it may still be from the middle (mm).
constexpr double kCentringSearchMm = 0.5;
// A full fit may stretch or shear the board this much at most (a share):
// more means a fiducial was mistaken, not that the machine is out.
constexpr double kMaxStretch = 0.01;

} // namespace

JPBoardLocator::Result JPBoardLocator::run(JPCell& cell, JPCameraFeed& feed, const JPBoard& board,
                                           const JPBoardSide& guess, const Options& o, const Progress& progress) {
    Result r;
    r.board = guess;
    const JPMountConfig& mount = feed.config().mount;
    const JPCameraCalibration cal = cell.cameraCalibration(feed.config().id);
    if (mount.axisX.empty() || mount.axisY.empty() || !cal.valid) {
        r.why = feed.config().name + " is not a calibrated camera on the head: calibrate it first";
        return r;
    }
    if (!cell.isHomed()) {
        r.why = "home the machine first";
        return r;
    }
    const std::vector<const JPPlacement*> fids = board.fiducials(guess.side);
    if (fids.size() < 2) {
        r.why = "the board has " + std::to_string(fids.size()) + " fiducial(s) on the side that is up; two are needed";
        return r;
    }

    // The order: the nearest the camera first, then the furthest from it (the
    // turn is best measured across the board), then each nearest the last.
    const auto here = cell.jogBase();
    double hx = here.at(mount.axisX) + mount.offsetX, hy = here.at(mount.axisY) + mount.offsetY;
    std::vector<const JPPlacement*> order;
    std::vector<const JPPlacement*> left = fids;
    auto machineOf = [&](const JPPlacement* p, double& x, double& y) { r.board.toMachine.apply(p->x, p->y, x, y); };
    auto takeNearest = [&](double x0, double y0, bool furthest) {
        size_t best = 0;
        double bestD = furthest ? -1 : 1e300;
        for (size_t i = 0; i < left.size(); ++i) {
            double x, y;
            machineOf(left[i], x, y);
            const double dd = std::hypot(x - x0, y - y0);
            if (furthest ? dd > bestD : dd < bestD) { bestD = dd; best = i; }
        }
        order.push_back(left[best]);
        left.erase(left.begin() + long(best));
    };
    takeNearest(hx, hy, false);
    { double x, y; machineOf(order[0], x, y); takeNearest(x, y, true); }
    while (!left.empty()) { double x, y; machineOf(order.back(), x, y); takeNearest(x, y, false); }

    const double scale = std::sqrt(cal.scaleX() * cal.scaleY());
    std::vector<JPPointFit::Pair> pairs;
    for (const JPPlacement* p : order) {
        Fiducial f;
        f.designator = p->designator;
        if (progress) progress(p->designator + " (" + std::to_string(r.fiducials.size() + 1) + " of " + std::to_string(order.size()) + ")");
        double vx, vy;
        machineOf(p, vx, vy);
        for (int move = 0; move < kCentringMoves; ++move) {
            if (!cell.moveAxesAndWait({ { mount.axisX, vx - mount.offsetX }, { mount.axisY, vy - mount.offsetY } }, o.speed, r.why))
                return r;
            JPGrayImage img;
            if (!JPCameraLook::settled(feed, img, r.why)) return r;
            if (img.width != cal.width || img.height != cal.height) {
                r.why = feed.config().name + " was calibrated at another picture size: calibrate it again";
                return r;
            }
            JPRoundMarkFinder::Request rq;
            rq.expectedX = img.width / 2.0;
            rq.expectedY = img.height / 2.0;
            rq.searchRadius = (move > 0 ? kCentringSearchMm : pairs.empty() ? o.firstSearchMm : o.searchMm) * scale;
            rq.diameter = (p->fiducialMm > 0 ? p->fiducialMm : o.fiducialDiameterMm) * scale;
            rq.polarity = JPRoundMarkFinder::Polarity::Bright;   // copper on solder mask
            const JPRoundMark m = JPCameraLook::findTryingHarder(cell, feed, img, rq);
            if (!m.found) {
                f.found = false;
                f.why = m.why;
                break;
            }
            double mx, my;
            cal.machinePoint(m.x, m.y, vx, vy, mx, my);
            const double off = std::hypot(mx - vx, my - vy);
            f.found = true;
            f.x = mx;
            f.y = my;
            vx = mx;
            vy = my;
            if (off < kCentredMm) break;
        }
        if (f.found) {
            pairs.push_back({ p->x, p->y, f.x, f.y });
            // Each find makes the guess better for the next.
            if (pairs.size() >= 3) {
                if (const auto a = JPPointFit::affine(pairs)) r.board.toMachine = a->map;
            } else if (const auto g = JPPointFit::rigid(pairs, guess.toMachine)) {
                r.board.toMachine = g->map;
            }
            JLOGC(JPlacerLog::kBoard, JLogLevel::Info) << p->designator << " found at " << f.x << ", " << f.y;
        } else {
            JLOGC(JPlacerLog::kBoard, JLogLevel::Warn) << p->designator << " not found: " << f.why;
        }
        r.fiducials.push_back(f);
    }

    if (pairs.size() < 2) {
        r.why = "only " + std::to_string(pairs.size()) + " fiducial(s) found; two are needed";
        return r;
    }
    // The final fit, and a check that it is a board and not a mistake.
    std::optional<JPPointFit::Result> fit;
    if (pairs.size() >= 3) {
        fit = JPPointFit::affine(pairs);
        r.affine = true;
    } else {
        fit = JPPointFit::rigid(pairs, guess.toMachine);
    }
    if (!fit) {
        r.why = "the fiducials found do not fix the board (all in a line?)";
        return r;
    }
    const JPAffine2D& m = fit->map;
    // Its stretch: the lengths of its axes, and how far from square they are.
    const double sx = std::hypot(m.a, m.c), sy = std::hypot(m.b, m.d);
    const double shear = std::abs(m.a * m.b + m.c * m.d) / (sx * sy);
    if (std::abs(sx - 1) > kMaxStretch || std::abs(sy - 1) > kMaxStretch || shear > kMaxStretch) {
        char buf[200];
        std::snprintf(buf, sizeof buf, "the fiducials found would stretch the board by %.1f%% and %.1f%%, or skew it "
                      "by %.2f deg: is one of them something else?", (sx - 1) * 100, (sy - 1) * 100, std::asin(shear) * 57.2958);
        r.why = buf;
        return r;
    }
    r.board.toMachine = m;
    r.rmsMm = fit->rms;
    if (r.affine) {
        // The board's own axes in machine coordinates (its mirror undone):
        // square on a square machine. Where Y leans, the board's Y axis is
        // seen tipped the other way: square X = axis X + xPerY axis Y.
        const JPAffine2D unmirrored = guess.side == JPPlacement::Side::Bottom ? m.after(JPAffine2D::mirrorX()) : m;
        r.xPerY = -(unmirrored.a * unmirrored.b + unmirrored.c * unmirrored.d)
                / (std::hypot(unmirrored.a, unmirrored.c) * std::hypot(unmirrored.b, unmirrored.d));
    }
    size_t k = 0;
    for (Fiducial& f : r.fiducials) {
        if (!f.found) continue;
        double x, y;
        m.apply(pairs[k].fromX, pairs[k].fromY, x, y);
        f.residualMm = std::hypot(x - f.x, y - f.y);
        ++k;
    }
    if (r.rmsMm > o.maxRmsMm) {
        char buf[160];
        std::snprintf(buf, sizeof buf, "the fiducials disagree with each other by %.3f mm (more than %.3f)", r.rmsMm, o.maxRmsMm);
        r.why = buf;
        return r;
    }
    r.ok = true;
    JLOGC(JPlacerLog::kBoard, JLogLevel::Info) << board.name << ": " << pairs.size() << " fiducial(s), turned "
        << m.rotationDeg() << " deg, fit " << r.rmsMm << " mm" << (r.affine ? " (affine)" : "");
    return r;
}

} // inline namespace jf
