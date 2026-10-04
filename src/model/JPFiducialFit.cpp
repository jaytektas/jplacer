// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPFiducialFit.h"

#include <algorithm>
#include <cmath>

inline namespace jf {

namespace {

struct M2 {
    double a = 0, b = 0, c = 0, d = 0;   // [[a, b], [c, d]]
    double det() const { return a * d - b * c; }
    M2 operator*(const M2& o) const {
        return { a * o.a + b * o.c, a * o.b + b * o.d, c * o.a + d * o.c, c * o.b + d * o.d };
    }
};

M2 rotation(double t) { return { std::cos(t), -std::sin(t), std::sin(t), std::cos(t) }; }

} // namespace

JPAffineTransform JPFiducialFit::derive(const std::vector<JPLocation>& expected, const std::vector<JPLocation>& measured) {
    const size_t n = std::min(expected.size(), measured.size());
    std::vector<double> sx(n), sy(n), dx(n), dy(n);
    double scx = 0, scy = 0, dcx = 0, dcy = 0;
    for (size_t i = 0; i < n; ++i) {
        const JPLocation s = expected[i].convertToUnits(JPLengthUnit::Millimeters);
        const JPLocation d = measured[i].convertToUnits(JPLengthUnit::Millimeters);
        sx[i] = s.x(), sy[i] = s.y(), dx[i] = d.x(), dy[i] = d.y();
        scx += sx[i], scy += sy[i], dcx += dx[i], dcy += dy[i];
    }
    JPAffineTransform out;
    if (n == 0) return out;
    scx /= double(n), scy /= double(n), dcx /= double(n), dcy /= double(n);
    // About the centroids: D·Sᵀ, and S·Sᵀ.
    M2 ds, ss;
    for (size_t i = 0; i < n; ++i) {
        const double px = sx[i] - scx, py = sy[i] - scy, qx = dx[i] - dcx, qy = dy[i] - dcy;
        ds.a += qx * px, ds.b += qx * py, ds.c += qy * px, ds.d += qy * py;
        ss.a += px * px, ss.b += px * py, ss.c += py * px, ss.d += py * py;
    }
    M2 linear;
    if (n > 2) {
        if (ds.det() < 0) {
            // A reflection: the singular vector of the smaller value turned round.
            const double e = (ds.a + ds.d) / 2, f = (ds.a - ds.d) / 2, g = (ds.c + ds.b) / 2, h = (ds.c - ds.b) / 2;
            const double q = std::hypot(e, h), r = std::hypot(f, g);
            const double a1 = std::atan2(g, f), a2 = std::atan2(h, e);
            const M2 sigma { q + r, 0, 0, -(q - r) };
            ds = rotation((a2 + a1) / 2) * sigma * rotation((a2 - a1) / 2);
        }
        const double det = ss.det();
        if (det == 0) return out;
        const M2 inv { ss.d / det, -ss.b / det, -ss.c / det, ss.a / det };
        linear = ds * inv;
    } else {
        // Two: the best rotation, and the scale of their spacing.
        const double angle = std::atan2(ds.c - ds.b, ds.a + ds.d);
        const double spacing = std::hypot(sx[0] - sx[n - 1], sy[0] - sy[n - 1]);
        const double scale = spacing > 0 ? std::hypot(dx[0] - dx[n - 1], dy[0] - dy[n - 1]) / spacing : 1.0;
        linear = rotation(angle);
        linear.a *= scale, linear.b *= scale, linear.c *= scale, linear.d *= scale;
    }
    out.m00 = linear.a, out.m01 = linear.b, out.m10 = linear.c, out.m11 = linear.d;
    out.m02 = dcx - (linear.a * scx + linear.b * scy);
    out.m12 = dcy - (linear.c * scx + linear.d * scy);
    return out;
}

} // inline namespace jf
