// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <j/config/Json.h>

#include <cmath>
#include <optional>
#include <string>
#include <vector>

inline namespace jf {

// An axis whose drive puts it a little off where it is sent, the same way
// every turn of its pulley or screw: a pulley not quite round on its shaft
// (eccentric) leads and lags the belt once a turn. Along the axis (`axis`)
// the head is `along(a)` further than the axis's coordinate `a` it was sent
// to; across it (`crossAxis`: a gantry driven at both ends by pulleys not
// turning in step twists) the other axis is `across(a)` further. Each is a
// wave of `periodMm` (the pulley's teeth times the belt's pitch; a screw's
// lead):
//   along(a)  = alongSin  * sin(2 pi a / period) + alongCos  * cos(2 pi a / period)
//   across(a) = acrossSin * sin(2 pi a / period) + acrossCos * cos(2 pi a / period)
// jplacer's coordinates are where the head really is; the axes are sent
// what puts it there (JPCell::toAxes), and read back the same way. Measured
// by a board scan: the head camera finding footprints all over a board,
// against where the board's design puts them (JPPeriodicErrorFit). None
// until measured, and none without its axes named.
struct JPPeriodicErrorConfig {
    std::string axis, crossAxis;
    double      periodMm = 0;
    double      alongSin = 0, alongCos = 0, acrossSin = 0, acrossCos = 0;

    // The last board scan, kept to show and to put in use: each footprint found (where the axis was, how far
    // off it was found along and across, the scan's fit there, and whether the fit left it out), and the
    // wave found (added to the one in use by Apply).
    struct Scan {
        struct Point {
            double at = 0, along = 0, across = 0, alongFit = 0, acrossFit = 0;
            bool   outlier = false;
        };
        std::string        when, board;
        double             periodMm = 0;
        double             alongSin = 0, alongCos = 0, acrossSin = 0, acrossCos = 0;   // the wave found (mm)
        double             scale = 1, rotationDeg = 0;   // the board's own, as fitted with it
        double             rmsBefore = 0, rmsAfter = 0;  // the footprints off (mm), without and with the wave
        bool               applied = false;
        std::vector<Point> points;
    };
    std::optional<Scan> lastScan;

    bool active() const {
        return !axis.empty() && periodMm > 0 && (alongSin != 0 || alongCos != 0 || acrossSin != 0 || acrossCos != 0);
    }
    double along(double a) const { return wave(alongSin, alongCos, a); }
    double across(double a) const { return wave(acrossSin, acrossCos, a); }
    // The axis's coordinate that puts the head at `place` along it (a few rounds settle it: the wave is far
    // smaller than its period).
    double axisFor(double place) const {
        double a = place;
        for (int i = 0; i < 6; ++i) a = place - along(a);
        return a;
    }
    double wave(double s, double c, double a) const {
        if (periodMm <= 0) return 0;
        const double t = 2 * M_PI * a / periodMm;
        return s * std::sin(t) + c * std::cos(t);
    }
    // How big the wave is along and across (its peak, mm).
    double alongSize() const { return std::hypot(alongSin, alongCos); }
    double acrossSize() const { return std::hypot(acrossSin, acrossCos); }

    static JPPeriodicErrorConfig fromJson(const JJson& j) {
        JPPeriodicErrorConfig p;
        p.axis = j["axis"].str();
        p.crossAxis = j["crossAxis"].str();
        p.periodMm = j["period"].number(0.0);
        p.alongSin = j["along"]["sin"].number(0.0);
        p.alongCos = j["along"]["cos"].number(0.0);
        p.acrossSin = j["across"]["sin"].number(0.0);
        p.acrossCos = j["across"]["cos"].number(0.0);
        if (const JJson& s = j["lastScan"]; s.isObject()) {
            Scan k;
            k.when = s["when"].str();
            k.board = s["board"].str();
            k.periodMm = s["period"].number(0.0);
            k.alongSin = s["along"]["sin"].number(0.0);
            k.alongCos = s["along"]["cos"].number(0.0);
            k.acrossSin = s["across"]["sin"].number(0.0);
            k.acrossCos = s["across"]["cos"].number(0.0);
            k.scale = s["scale"].number(1.0);
            k.rotationDeg = s["rotation"].number(0.0);
            k.rmsBefore = s["rmsBefore"].number(0.0);
            k.rmsAfter = s["rmsAfter"].number(0.0);
            k.applied = s["applied"].boolean(false);
            const JJson& pts = s["points"];
            for (size_t i = 0; i < pts.size(); ++i) {
                const JJson& q = pts[i];
                auto n = [&q](size_t at) { return q[at].number(0.0); };
                k.points.push_back({ n(0), n(1), n(2), n(3), n(4), q[size_t(5)].boolean(false) });
            }
            p.lastScan = k;
        }
        return p;
    }
    JJson toJson() const {
        JJson j = JJson::object();
        j["axis"] = axis;
        j["crossAxis"] = crossAxis;
        j["period"] = periodMm;
        j["along"]["sin"] = alongSin;
        j["along"]["cos"] = alongCos;
        j["across"]["sin"] = acrossSin;
        j["across"]["cos"] = acrossCos;
        if (lastScan) {
            const Scan& k = *lastScan;
            JJson s = JJson::object();
            s["when"] = k.when;
            s["board"] = k.board;
            s["period"] = k.periodMm;
            s["along"]["sin"] = k.alongSin;
            s["along"]["cos"] = k.alongCos;
            s["across"]["sin"] = k.acrossSin;
            s["across"]["cos"] = k.acrossCos;
            s["scale"] = k.scale;
            s["rotation"] = k.rotationDeg;
            s["rmsBefore"] = k.rmsBefore;
            s["rmsAfter"] = k.rmsAfter;
            s["applied"] = k.applied;
            JJson pts = JJson::array();
            for (const Scan::Point& q : k.points) {
                JJson e = JJson::array();
                e.push(q.at);
                e.push(q.along);
                e.push(q.across);
                e.push(q.alongFit);
                e.push(q.acrossFit);
                e.push(q.outlier);
                pts.push(e);
            }
            s["points"] = pts;
            j["lastScan"] = s;
        }
        return j;
    }
};

} // inline namespace jf
