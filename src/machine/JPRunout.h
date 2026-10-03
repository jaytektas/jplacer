// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <j/config/Json.h>

#include <optional>
#include <string>
#include <vector>

inline namespace jf {

// A nozzle tip's runout on one nozzle: its end does not sit on the rotation
// axis, so turning the nozzle swings it round a circle. Measured with the
// camera looking up, the tip's centre at each of several angles (offsets from
// where the nozzle was sent, mm), and fitted:
//
//   offset(a) = centre + radius (cos(a - phase), sin(a - phase))
//
// by linear least squares (the centre, and radius cos / sin phase, are
// linear in the measurements). Compensated, a move to angle `a` is sent
// runoutAt(a) the other way, so the tip's centre lands where it was sent.
// The centre is not compensated: it is where the nozzle's axis is seen from
// where the camera's position and the nozzle's offset say, so it tells how
// far those are off.
struct JPRunout {
    struct Point { double angle = 0, dx = 0, dy = 0; };

    double             centreX = 0, centreY = 0;
    double             radius = 0, phaseDeg = 0;
    double             rmsMm = 0, peakMm = 0;   // the measurements against the fit
    std::string        when;
    std::vector<Point> points;

    // The swing at angle `a` (degrees): the offset less the centre.
    void runoutAt(double a, double& dx, double& dy) const;
    // The fit through `points` (three or more at different angles); nothing
    // when they do not fix it.
    static std::optional<JPRunout> fit(const std::vector<Point>& points);

    static JPRunout fromJson(const JJson& j);
    JJson toJson() const;
};

} // inline namespace jf
