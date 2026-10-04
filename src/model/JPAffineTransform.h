// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <optional>

inline namespace jf {

// A 2D affine transform as Java's AffineTransform, which OpenPnP places
// boards and panels with: translate, rotate and scale act before what is
// already there; preConcatenate acts after it.
//
//   x' = m00 x + m01 y + m02
//   y' = m10 x + m11 y + m12
class JPAffineTransform {
public:
    double m00 = 1, m10 = 0, m01 = 0, m11 = 1, m02 = 0, m12 = 0;

    void translate(double tx, double ty);
    void rotate(double radians);
    void scale(double sx, double sy);
    void shear(double shx, double shy);
    // this = this * t (t first).
    void concatenate(const JPAffineTransform& t);
    // this = t * this (t after).
    void preConcatenate(const JPAffineTransform& t);
    std::optional<JPAffineTransform> inverse() const;
    void apply(double x, double y, double& ox, double& oy) const;

    // OpenPnP's Utils2D.AffineInfo: what the transform does, taken apart.
    struct Info {
        double xScale = 1, yScale = 1, xShear = 0, rotationAngleDeg = 0, xTranslation = 0, yTranslation = 0;
    };
    Info info() const;
    static JPAffineTransform fromInfo(const Info& i);
};

} // inline namespace jf
