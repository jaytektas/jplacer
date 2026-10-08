// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <cmath>

inline namespace jf {

// Rotations in degrees, as placements keep them: within (-180, 180].
class JPAngles {
public:
    static double normalise(double deg) {
        double r = std::fmod(deg, 360.0);
        if (r > 180) r -= 360;
        if (r <= -180) r += 360;
        return r;
    }
    // How far `b` is turned from `a`, the short way round.
    static double difference(double a, double b) { return normalise(b - a); }
};

} // inline namespace jf
