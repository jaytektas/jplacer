// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <optional>

inline namespace jf {

// A map of the plane: (x, y) -> (a x + b y + tx, c x + d y + ty). Places a
// board on the machine (turned, mirrored for its bottom side, moved), and,
// fitted to fiducials, also takes up what is not quite square or to scale.
struct JPAffine2D {
    double a = 1, b = 0, c = 0, d = 1;
    double tx = 0, ty = 0;

    void apply(double x, double y, double& ox, double& oy) const {
        ox = a * x + b * y + tx;
        oy = c * x + d * y + ty;
    }
    // This after `first`: (this * first)(p) = this(first(p)).
    JPAffine2D after(const JPAffine2D& first) const {
        JPAffine2D r;
        r.a  = a * first.a + b * first.c;
        r.b  = a * first.b + b * first.d;
        r.c  = c * first.a + d * first.c;
        r.d  = c * first.b + d * first.d;
        r.tx = a * first.tx + b * first.ty + tx;
        r.ty = c * first.tx + d * first.ty + ty;
        return r;
    }
    // Nothing when it flattens the plane.
    std::optional<JPAffine2D> inverse() const;
    // How far it turns, degrees: the turn of the X axis, or for a mirrored map
    // (a board's bottom side) the turn after the mirror.
    double rotationDeg() const;
    bool mirrored() const { return a * d - b * c < 0; }

    // Turned by `degrees` (anticlockwise), then moved by (x, y).
    static JPAffine2D placed(double x, double y, double degrees);
    // x -> -x: a board seen from its other side.
    static JPAffine2D mirrorX();
};

} // inline namespace jf
