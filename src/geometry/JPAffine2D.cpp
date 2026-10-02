// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPAffine2D.h"

#include <cmath>

inline namespace jf {

namespace {

constexpr double kPi = 3.14159265358979323846;

} // namespace

std::optional<JPAffine2D> JPAffine2D::inverse() const {
    const double det = a * d - b * c;
    if (std::abs(det) < 1e-15) return std::nullopt;
    JPAffine2D r;
    r.a = d / det;
    r.b = -b / det;
    r.c = -c / det;
    r.d = a / det;
    r.tx = -(r.a * tx + r.b * ty);
    r.ty = -(r.c * tx + r.d * ty);
    return r;
}

double JPAffine2D::rotationDeg() const {
    return (mirrored() ? std::atan2(-c, -a) : std::atan2(c, a)) * 180 / kPi;
}

JPAffine2D JPAffine2D::placed(double x, double y, double degrees) {
    const double r = degrees * kPi / 180, cs = std::cos(r), sn = std::sin(r);
    JPAffine2D t;
    t.a = cs;
    t.b = -sn;
    t.c = sn;
    t.d = cs;
    t.tx = x;
    t.ty = y;
    return t;
}

JPAffine2D JPAffine2D::mirrorX() {
    JPAffine2D t;
    t.a = -1;
    return t;
}

} // inline namespace jf
