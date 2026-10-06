// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <cmath>

inline namespace jf {

// How a camera's lens bends straight lines: radially about the lens's centre
// (on a small camera rarely the middle of the picture: the sensor sits a
// little off the lens's axis). A point at distance r from that centre is seen
// at r (1 + k1 (r/R)^2 + k2 (r/R)^4), R half the picture's diagonal. k1 below
// zero is barrel (the usual wide lens: the edges pulled in), above zero
// pincushion; k2 is how that changes towards the corners.
// Positions are pixels in the picture.
class JPLens {
public:
    double k1 = 0, k2 = 0;
    double centreX = 0, centreY = 0;
    double radiusPx = 0;   // R; zero: no lens model (straight)

    // A lens for a picture width x height, centred on the picture's middle
    // unless a centre is given.
    static JPLens forPicture(int width, int height, double k1) {
        return forPicture(width, height, k1, width / 2.0, height / 2.0);
    }
    static JPLens forPicture(int width, int height, double k1, double centreX, double centreY, double k2 = 0) {
        JPLens l;
        l.k1 = k1;
        l.k2 = k2;
        l.centreX = centreX;
        l.centreY = centreY;
        l.radiusPx = 0.5 * std::hypot(double(width), double(height));
        return l;
    }

    // Where a point that would be at (ux, uy) through a perfect lens is seen.
    void distort(double ux, double uy, double& x, double& y) const {
        const double dx = ux - centreX, dy = uy - centreY;
        const double f = factor(dx * dx + dy * dy);
        x = centreX + dx * f;
        y = centreY + dy * f;
    }

    // Where a point seen at (x, y) would be through a perfect lens.
    void undistort(double x, double y, double& ux, double& uy) const {
        // A perfect lens: as it is.
        if (k1 == 0 && k2 == 0) {
            ux = x;
            uy = y;
            return;
        }
        // r_seen = r f(r): solved for r by fixed-point steps, quick for any
        // lens that does not fold the picture over itself.
        const double sx = x - centreX, sy = y - centreY;
        double dx = sx, dy = sy;
        for (int i = 0; i < kSteps; ++i) {
            const double f = factor(dx * dx + dy * dy);
            dx = sx / f;
            dy = sy / f;
        }
        ux = centreX + dx;
        uy = centreY + dy;
    }

private:
    static constexpr int kSteps = 12;

    double factor(double r2) const {
        if (radiusPx <= 0) return 1;
        const double n = r2 / (radiusPx * radiusPx);
        return 1 + k1 * n + k2 * n * n;
    }
};

} // inline namespace jf
