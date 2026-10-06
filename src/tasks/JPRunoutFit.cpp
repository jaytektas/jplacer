// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPRunoutFit.h"

#include "model/JPFiducialFit.h"

#include <algorithm>
#include <cmath>

inline namespace jf {

namespace {

constexpr double kRad = M_PI / 180;
// The runout a tip is expected to have for the affine fit (mm): its scale is then the runout itself.
constexpr double kExpectedRunoutMm = 1.0;

// OpenPnP's Utils2D.normalizeAngle180.
double normalize180(double a) {
    while (a > 180) a -= 360;
    while (a <= -180) a += 360;
    return a;
}

// OpenPnP's calcCircleFitKasa (a port of http://people.cas.uab.edu/~mosya/cl/CPPcircle.html).
void kasa(const std::vector<JPRunout::Point>& points, JPRunout& r) {
    const double n = double(points.size());
    double meanX = 0, meanY = 0;
    for (const JPRunout::Point& p : points) {
        meanX += p.dx;
        meanY += p.dy;
    }
    meanX /= n;
    meanY /= n;
    double mxx = 0, myy = 0, mxy = 0, mxz = 0, myz = 0;
    for (const JPRunout::Point& p : points) {
        const double xi = p.dx - meanX, yi = p.dy - meanY, zi = xi * xi + yi * yi;
        mxx += xi * xi;
        myy += yi * yi;
        mxy += xi * yi;
        mxz += xi * zi;
        myz += yi * zi;
    }
    mxx /= n;
    myy /= n;
    mxy /= n;
    mxz /= n;
    myz /= n;
    // The equations solved by Cholesky factorization.
    const double g11 = std::sqrt(mxx);
    const double g12 = mxy / g11;
    const double g22 = std::sqrt(myy - g12 * g12);
    const double d1 = mxz / g11;
    const double d2 = (myz - d1 * g12) / g22;
    const double c = d2 / g22 / 2.0;
    const double b = (d1 - g12 * c) / g11 / 2.0;
    const double cx = b + meanX, cy = c + meanY, radius = std::sqrt(b * b + c * c + mxx + myy);
    if (!std::isnan(cx) && !std::isnan(cy) && !std::isnan(radius)) {
        r.centreX = cx;
        r.centreY = cy;
        r.radius = radius;
    } else {
        // All the same (a simulated machine with no runout): no runout, the offset constant.
        r.centreX = points.front().dx;
        r.centreY = points.front().dy;
        r.radius = 0;
    }
}

// OpenPnP's calcPhaseShift: each measurement's angle less the angle it was found at about the centre, unwrapped
// against the one before, averaged, normalized.
void phase(const std::vector<JPRunout::Point>& points, JPRunout& r) {
    double prior = 0, sum = 0;
    for (const JPRunout::Point& p : points) {
        const double found = std::atan2(p.dy - r.centreY, p.dx - r.centreX) / kRad;
        double difference = p.angle - found;
        while (prior - difference > 180) difference += 360;
        while (prior - difference < -180) difference -= 360;
        prior = difference;
        sum += difference;
    }
    r.phaseDeg = normalize180(sum / double(points.size()));
}

bool affine(const std::vector<JPRunout::Point>& points, JPRunout& r) {
    std::vector<JPLocation> expected, measured;
    for (const JPRunout::Point& p : points) {
        const double a = p.angle * kRad;
        expected.emplace_back(JPLengthUnit::Millimeters, kExpectedRunoutMm * std::cos(a), kExpectedRunoutMm * std::sin(a), 0, p.angle);
        measured.emplace_back(JPLengthUnit::Millimeters, p.dx, p.dy, 0, p.angle);
    }
    const JPAffineTransform::Info i = JPFiducialFit::derive(expected, measured).info();
    if (!std::isfinite(i.xScale) || !std::isfinite(i.yScale)) return false;
    r.radius = std::sqrt(std::max(0.0, i.xScale) * std::max(0.0, i.yScale)) / kExpectedRunoutMm;
    r.phaseDeg = -i.rotationAngleDeg;
    r.centreX = i.xTranslation;
    r.centreY = i.yTranslation;
    return true;
}

} // namespace

std::optional<JPRunout> JPRunoutFit::fit(const std::vector<JPRunout::Point>& points, const std::string& algorithm) {
    const auto& names = JPRunout::algorithms();
    if (points.size() < 3 || std::find(names.begin(), names.end(), algorithm) == names.end()) return std::nullopt;
    JPRunout r;
    r.algorithm = algorithm;
    r.points = points;
    if (r.table()) {
        // Shown as OpenPnP shows a table: its first offset.
        r.centreX = points.front().dx;
        r.centreY = points.front().dy;
        return r;
    }
    if (algorithm.size() > 6 && algorithm.compare(algorithm.size() - 6, 6, "Affine") == 0) {
        if (!affine(points, r)) return std::nullopt;
    } else {
        kasa(points, r);
        phase(points, r);
    }
    r.estimateError();
    return r;
}

} // inline namespace jf
