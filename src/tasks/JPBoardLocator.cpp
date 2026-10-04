// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPBoardLocator.h"

#include "JPCameraLook.h"
#include "JPPadPattern.h"

#include "common/JPlacerLog.h"
#include "geometry/JPPointFit.h"
#include "vision/JPPatternFinder.h"
#include "vision/JPRoundMarkFinder.h"

#include <j/core/Log.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

inline namespace jf {

namespace {

// Once the camera is over it, how far it may still be from where it is
// expected (mm).
constexpr double kCentringSearchMm = 0.5;
// A full fit may stretch or shear the board this much at most (a share):
// more means a reference was mistaken, not that the machine is out.
constexpr double kMaxStretch = 0.01;
// A learned look is used only at the scale it was taken at, to this share.
constexpr double kLookScaleShare = 0.05;

constexpr double kPi = 3.14159265358979323846;

// A reference's learned look, as a picture.
JPGrayImage lookOf(const JPPlacement& p) {
    JPGrayImage img;
    if (p.lookWidth <= 0 || p.look.empty()) return img;
    img.width = p.lookWidth;
    img.height = int(p.look.size() / size_t(p.lookWidth));
    for (int k = 0; k < img.width * img.height; ++k) img.pixels.push_back(float(p.look[size_t(k)]));
    return img;
}

// The fit of `pairs`, checked to be a board and not a mistake; `r` filled.
bool fitted(const std::vector<JPPointFit::Pair>& pairs, const JPBoardSide& guess, const JPBoardLocator::Options& o,
            const std::string& what, JPBoardLocator::Result& r) {
    if (pairs.size() < 2) {
        r.why = "only " + std::to_string(pairs.size()) + " reference(s) " + what + "; two are needed";
        return false;
    }
    std::optional<JPPointFit::Result> fit;
    if (pairs.size() >= 3) {
        fit = JPPointFit::affine(pairs);
        r.affine = true;
    } else {
        fit = JPPointFit::rigid(pairs, guess.toMachine);
    }
    if (!fit) {
        r.why = "the references " + what + " do not fix the board (all in a line?)";
        return false;
    }
    const JPAffine2D& m = fit->map;
    // Its stretch: the lengths of its axes, and how far from square they are.
    const double sx = std::hypot(m.a, m.c), sy = std::hypot(m.b, m.d);
    const double shear = std::abs(m.a * m.b + m.c * m.d) / (sx * sy);
    if (std::abs(sx - 1) > kMaxStretch || std::abs(sy - 1) > kMaxStretch || shear > kMaxStretch) {
        char buf[200];
        std::snprintf(buf, sizeof buf, "the references %s would stretch the board by %.1f%% and %.1f%%, or skew it "
                      "by %.2f deg: is one of them something else?", what.c_str(), (sx - 1) * 100, (sy - 1) * 100,
                      std::asin(shear) * 57.2958);
        r.why = buf;
        return false;
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
    for (JPBoardLocator::Reference& f : r.references) {
        if (!f.found) continue;
        double x, y;
        m.apply(pairs[k].fromX, pairs[k].fromY, x, y);
        f.residualMm = std::hypot(x - f.x, y - f.y);
        ++k;
    }
    if (r.rmsMm > o.maxRmsMm) {
        char buf[160];
        std::snprintf(buf, sizeof buf, "the references disagree with each other by %.3f mm (more than %.3f)", r.rmsMm, o.maxRmsMm);
        r.why = buf;
        return false;
    }
    return true;
}

} // namespace

JPBoardLocator::Result JPBoardLocator::run(JPCell& cell, JPCameraFeed& feed, const JPBoard& board,
                                           const JPBoardSide& guess, const Options& o, const Progress& progress) {
    Result r;
    r.board = guess;
    const JPMountConfig& mount = feed.config().mount;
    if (mount.axisX.empty() || mount.axisY.empty()) {
        r.why = feed.config().name + " is not a camera on the head";
        return r;
    }
    JPCameraCalibration cal;
    if (!JPCameraLook::calibration(cell, feed, cal, r.why)) return r;
    if (!cell.isHomed()) {
        r.why = "home the machine first";
        return r;
    }
    const std::vector<const JPPlacement*> refs = board.references(guess.side);
    if (refs.size() < 2) {
        r.why = "the board has " + std::to_string(refs.size()) + " reference(s) on the side that is up; two are needed";
        return r;
    }

    // The order: the nearest the camera first, then the furthest from it (the
    // turn is best measured across the board), then each nearest the last.
    const auto here = cell.jogBase();
    double hx = here.at(mount.axisX) + mount.offsetX, hy = here.at(mount.axisY) + mount.offsetY;
    std::vector<const JPPlacement*> order;
    std::vector<const JPPlacement*> left = refs;
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
    // Parallax: the two places looked from, either side of a fiducial.
    const JPFiducialConfig& fc = o.fiducials;
    const double half = fc.parallaxDiameterMm / 2, turn = fc.parallaxAngleDeg * kPi / 180;
    const double px = half * std::cos(turn), py = half * std::sin(turn);
    // Where the camera looks now: the nearer parallax place is looked from first.
    double cx = hx, cy = hy;
    std::vector<JPPointFit::Pair> pairs;
    for (const JPPlacement* p : order) {
        Reference f;
        f.designator = p->designator;
        if (progress) progress(p->designator + " (" + std::to_string(r.references.size() + 1) + " of " + std::to_string(order.size()) + ")");
        // How it is found: a fiducial as a round mark, a part by its pads, else by its learned look.
        JPFootprint footprint;
        double degrees = 0;
        const bool round = p->fiducial;
        const bool pads = !round && o.footprintOf && o.footprintOf(*p, footprint, degrees) && !footprint.pads.empty();
        const JPGrayImage look = round || pads ? JPGrayImage() : lookOf(*p);
        if (!round && !pads) {
            if (look.pixels.empty()) f.why = "no way to find it by camera (no package footprint, no learned look): record it by hand";
            else if (std::abs(p->lookPxPerMm / scale - 1) > kLookScaleShare) f.why = "its learned look was taken at another scale: record it again";
            if (!f.why.empty()) {
                JLOGC(JPlacerLog::kBoard, JLogLevel::Warn) << p->designator << ": " << f.why;
                r.references.push_back(f);
                continue;
            }
        }
        double vx, vy;
        machineOf(p, vx, vy);
        // Find it from the camera looking at (lx, ly), expecting it at (vx, vy):
        // where it is, machine.
        auto findFrom = [&](double lx, double ly, double searchMm, double& mx, double& my) {
            if (!cell.moveAxesAndWait({ { mount.axisX, lx - mount.offsetX }, { mount.axisY, ly - mount.offsetY } }, o.speed, r.why))
                return false;
            cx = lx;
            cy = ly;
            JPGrayImage img;
            if (!JPCameraLook::settled(feed, img, r.why)) return false;
            if (img.width != cal.width || img.height != cal.height) {
                r.why = feed.config().name + " changed its picture size while in use";
                return false;
            }
            double ex, ey;
            if (!cal.pixelFor(vx, vy, lx, ly, ex, ey)) {
                ex = img.width / 2.0;
                ey = img.height / 2.0;
            }
            double fx, fy;
            if (round) {
                JPRoundMarkFinder::Request rq;
                rq.expectedX = ex;
                rq.expectedY = ey;
                rq.searchRadius = searchMm * scale;
                rq.diameter = (p->fiducialMm > 0 ? p->fiducialMm : o.fiducialDiameterMm) * scale;
                rq.polarity = JPRoundMarkFinder::Polarity::Bright;   // copper on solder mask
                const JPRoundMark m = JPCameraLook::findTryingHarder(cell, feed, img, rq);
                if (!m.found) {
                    f.why = m.why;
                    return false;
                }
                fx = m.x;
                fy = m.y;
            } else {
                JPGrayImage pattern = look;
                double pcx = look.width / 2.0, pcy = look.height / 2.0;
                if (pads && !JPPadPattern::draw(cal, lx, ly, r.board.toMachine, *p, footprint, degrees, ex, ey, pattern, pcx, pcy)) {
                    f.why = "its pads could not be drawn as the camera sees them";
                    return false;
                }
                JPPatternFinder::Request rq;
                rq.expectedX = ex;
                rq.expectedY = ey;
                rq.searchRadius = searchMm * scale;
                const JPPatternFinder::Result m = JPPatternFinder::find(img, pattern, pcx, pcy, rq);
                if (!m.found) {
                    f.why = m.why;
                    return false;
                }
                fx = m.x;
                fy = m.y;
            }
            if (cal.machinePoint(fx, fy, lx, ly, mx, my)) return true;
            f.why = "seen where the calibration cannot place it";
            return false;
        };
        for (int pass = 0; pass < std::max(1, fc.passes); ++pass) {
            // Widely until two are found: until then the board's turn is a
            // guess, and the second is the furthest from the first.
            const double searchMm = pass > 0 ? kCentringSearchMm : pairs.size() < 2 ? o.firstSearchMm : o.searchMm;
            double mx = 0, my = 0;
            bool found;
            if (round && half > 0) {
                // From both sides, the nearer first; the midpoint cancels
                // what looking from the side puts out.
                const double s = std::hypot(vx + px - cx, vy + py - cy) <= std::hypot(vx - px - cx, vy - py - cy) ? 1 : -1;
                double ax, ay, bx, by;
                found = findFrom(vx + s * px, vy + s * py, searchMm, ax, ay)
                     && findFrom(vx - s * px, vy - s * py, searchMm, bx, by);
                mx = (ax + bx) / 2;
                my = (ay + by) / 2;
            } else {
                found = findFrom(vx, vy, searchMm, mx, my);
            }
            if (!found) {
                if (!r.why.empty()) return r;   // the machine or camera failed, not the finding
                f.found = false;
                break;
            }
            const double off = std::hypot(mx - vx, my - vy);
            f.found = true;
            f.x = mx;
            f.y = my;
            vx = mx;
            vy = my;
            if (off < fc.centredMm) break;
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
        r.references.push_back(f);
    }

    if (!fitted(pairs, guess, o, "found", r)) return r;
    r.ok = true;
    JLOGC(JPlacerLog::kBoard, JLogLevel::Info) << board.name << ": " << pairs.size() << " reference(s), turned "
        << r.board.toMachine.rotationDeg() << " deg, fit " << r.rmsMm << " mm" << (r.affine ? " (affine)" : "");
    return r;
}

JPBoardLocator::Result JPBoardLocator::searchStart(JPCell& cell, JPCameraFeed& feed, const JPBoard& board,
                                                   const JPBoardSide& guess, double x0, double y0, double x1, double y1,
                                                   const Options& o, const Progress& progress) {
    Result r;
    r.board = guess;
    const JPMountConfig& mount = feed.config().mount;
    JPCameraCalibration cal;
    if (mount.axisX.empty() || mount.axisY.empty()) {
        r.why = feed.config().name + " is not a camera on the head";
        return r;
    }
    if (!JPCameraLook::calibration(cell, feed, cal, r.why)) return r;
    if (!cell.isHomed()) {
        r.why = "home the machine first";
        return r;
    }
    // The round-mark reference nearest the board's origin.
    const JPPlacement* first = nullptr;
    for (const JPPlacement* p : board.references(guess.side))
        if (p->fiducial && (!first || std::hypot(p->x, p->y) < std::hypot(first->x, first->y))) first = p;
    if (!first) {
        r.why = "a region search looks for a round mark: mark a fiducial on this side as a reference";
        return r;
    }
    const double scale = std::sqrt(cal.scaleX() * cal.scaleY());
    // Each picture overlaps the last by a fifth, so a mark on an edge is whole in one of them.
    const double stepX = 0.8 * cal.width / scale, stepY = 0.8 * cal.height / scale;
    const int cols = std::max(1, int(std::ceil((x1 - x0) / stepX))), rows = std::max(1, int(std::ceil((y1 - y0) / stepY)));
    for (int row = 0; row <= rows; ++row)
        for (int col = 0; col <= cols; ++col) {
            const double lx = std::min(x1, x0 + col * stepX), ly = std::min(y1, y0 + row * stepY);
            if (progress) progress("looking for " + first->designator + ": picture " + std::to_string(row * (cols + 1) + col + 1)
                                   + " of " + std::to_string((rows + 1) * (cols + 1)));
            if (!cell.moveAxesAndWait({ { mount.axisX, lx - mount.offsetX }, { mount.axisY, ly - mount.offsetY } }, o.speed, r.why))
                return r;
            JPGrayImage img;
            if (!JPCameraLook::settled(feed, img, r.why)) return r;
            JPRoundMarkFinder::Request rq;
            rq.expectedX = img.width / 2.0;
            rq.expectedY = img.height / 2.0;
            rq.searchRadius = std::hypot(img.width, img.height) / 2;
            rq.diameter = (first->fiducialMm > 0 ? first->fiducialMm : o.fiducialDiameterMm) * scale;
            rq.polarity = JPRoundMarkFinder::Polarity::Bright;
            const JPRoundMark m = JPRoundMarkFinder::find(img, rq);
            double mx, my;
            if (!m.found || !cal.machinePoint(m.x, m.y, lx, ly, mx, my)) continue;
            // The board moved so this reference is there, its side and turn kept.
            JPBoardSide b = JPBoardSide::placed(guess.side, 0, 0, guess.toMachine.rotationDeg());
            double fx, fy;
            b.toMachine.apply(first->x, first->y, fx, fy);
            b.toMachine.tx = mx - fx;
            b.toMachine.ty = my - fy;
            r.board = b;
            r.ok = true;
            r.references.push_back({ first->designator, true, mx, my, 0, {} });
            JLOGC(JPlacerLog::kBoard, JLogLevel::Info) << first->designator << " taken to be the round mark at " << mx << ", " << my;
            return r;
        }
    r.why = "no round mark of " + first->designator + "'s size in the region searched";
    return r;
}

JPBoardLocator::Result JPBoardLocator::fitRecorded(const JPBoard& board, const JPBoardSide& guess, const Options& o) {
    Result r;
    r.board = guess;
    std::vector<JPPointFit::Pair> pairs;
    for (const JPPlacement* p : board.references(guess.side)) {
        Reference f;
        f.designator = p->designator;
        f.found = p->recorded;
        f.x = p->recordedX;
        f.y = p->recordedY;
        if (!p->recorded) f.why = "not recorded yet";
        else pairs.push_back({ p->x, p->y, p->recordedX, p->recordedY });
        r.references.push_back(f);
    }
    if (!fitted(pairs, guess, o, "recorded", r)) return r;
    r.ok = true;
    JLOGC(JPlacerLog::kBoard, JLogLevel::Info) << board.name << ": fitted to " << pairs.size() << " recorded reference(s), fit "
                                               << r.rmsMm << " mm";
    return r;
}

} // inline namespace jf
