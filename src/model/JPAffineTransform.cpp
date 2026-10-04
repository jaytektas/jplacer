// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPAffineTransform.h"

#include <cmath>

inline namespace jf {

void JPAffineTransform::concatenate(const JPAffineTransform& t) {
    const double a = m00 * t.m00 + m01 * t.m10;
    const double b = m00 * t.m01 + m01 * t.m11;
    const double c = m00 * t.m02 + m01 * t.m12 + m02;
    const double d = m10 * t.m00 + m11 * t.m10;
    const double e = m10 * t.m01 + m11 * t.m11;
    const double f = m10 * t.m02 + m11 * t.m12 + m12;
    m00 = a; m01 = b; m02 = c; m10 = d; m11 = e; m12 = f;
}

void JPAffineTransform::preConcatenate(const JPAffineTransform& t) {
    JPAffineTransform r = t;
    r.concatenate(*this);
    *this = r;
}

void JPAffineTransform::translate(double tx, double ty) {
    JPAffineTransform t;
    t.m02 = tx;
    t.m12 = ty;
    concatenate(t);
}

void JPAffineTransform::rotate(double radians) {
    JPAffineTransform t;
    double s = std::sin(radians), c = std::cos(radians);
    // As Java: exact quarter turns stay exact.
    if (s == 1.0 || s == -1.0) c = 0.0;
    else if (c == 1.0 || c == -1.0) s = 0.0;
    t.m00 = c; t.m01 = -s; t.m10 = s; t.m11 = c;
    concatenate(t);
}

void JPAffineTransform::scale(double sx, double sy) {
    JPAffineTransform t;
    t.m00 = sx;
    t.m11 = sy;
    concatenate(t);
}

void JPAffineTransform::shear(double shx, double shy) {
    JPAffineTransform t;
    t.m01 = shx;
    t.m10 = shy;
    concatenate(t);
}

std::optional<JPAffineTransform> JPAffineTransform::inverse() const {
    const double det = m00 * m11 - m01 * m10;
    if (det == 0 || !std::isfinite(det)) return std::nullopt;
    JPAffineTransform r;
    r.m00 = m11 / det;
    r.m01 = -m01 / det;
    r.m10 = -m10 / det;
    r.m11 = m00 / det;
    r.m02 = (m01 * m12 - m11 * m02) / det;
    r.m12 = (m10 * m02 - m00 * m12) / det;
    return r;
}

void JPAffineTransform::apply(double x, double y, double& ox, double& oy) const {
    ox = m00 * x + m01 * y + m02;
    oy = m10 * x + m11 * y + m12;
}

JPAffineTransform::Info JPAffineTransform::info() const {
    Info i;
    const double xSign = (m00 * m11 - m10 * m01) > 0 ? 1.0 : (m00 * m11 - m10 * m01) < 0 ? -1.0 : 0.0;
    const double theta = std::atan2(xSign * m10, xSign * m00);
    i.rotationAngleDeg = theta * 180 / M_PI;
    const double s = std::sin(theta), c = std::cos(theta);
    i.xScale = xSign * std::sqrt(m00 * m00 + m10 * m10);
    const double shsy = m01 * c + m11 * s;
    if (std::fabs(c) != 1.0) i.yScale = (shsy * c - m01) / s;
    else i.yScale = (m11 - shsy * s) / c;
    i.xShear = shsy / i.yScale;
    i.xTranslation = m02;
    i.yTranslation = m12;
    return i;
}

JPAffineTransform JPAffineTransform::fromInfo(const Info& i) {
    JPAffineTransform t;
    t.translate(i.xTranslation, i.yTranslation);
    t.rotate(i.rotationAngleDeg * M_PI / 180);
    t.shear(i.xShear, 0.0);
    t.scale(i.xScale, i.yScale);
    return t;
}

} // inline namespace jf
