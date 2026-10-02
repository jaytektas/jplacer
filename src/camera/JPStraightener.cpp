// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPStraightener.h"

#include <algorithm>
#include <cmath>
#include <utility>

inline namespace jf {

namespace {

// The edge of a picture is checked every so many pixels when fitting the
// straightened picture to it.
constexpr int kEdgeStepPx = 8;
// The enlargement searched for lies between these, halved so many times.
constexpr double kMinZoom = 0.25, kMaxZoom = 4;
constexpr int kZoomSteps = 40;
// How near a point must straighten back to where it was bent from (pixels):
// beyond the picture the lens's bending folds back on itself.
constexpr double kRoundTripPx = 0.01;

} // namespace

std::optional<JPStraightener> JPStraightener::make(const JPCameraCalibration& cal, bool lookingUp, double showAll) {
    if (!cal.valid || cal.width <= 0 || cal.height <= 0) return std::nullopt;
    const double det = cal.pxPerMm[0] * cal.pxPerMm[3] - cal.pxPerMm[1] * cal.pxPerMm[2];
    if (std::abs(det) < 1e-12) return std::nullopt;
    JPStraightener s;
    s.m_cal = cal;
    s.m_lens = cal.lens();
    s.m_w = cal.width;
    s.m_h = cal.height;
    s.m_lens.undistort(cal.width / 2.0, cal.height / 2.0, s.m_u0x, s.m_u0y);
    // Square, at the camera's mean scale: looking down [-k 0; 0 k], looking up [k 0; 0 k].
    const double k = std::sqrt(std::abs(det));
    auto setZoom = [&s, k, lookingUp](double z) {
        s.m_scaleX = (lookingUp ? k : -k) * z;
        s.m_scaleY = k * z;
    };
    auto inside = [&s](double x, double y) { return x >= 0 && y >= 0 && x <= s.m_w - 1 && y <= s.m_h - 1; };
    // At enlargement z: does every straightened edge pixel have picture behind
    // it; is every edge pixel of the picture as taken inside the straightened one.
    auto straightFilled = [&](double z) {
        setZoom(z);
        double rx, ry;
        for (int x = 0; x < s.m_w; x += kEdgeStepPx)
            for (const int y : { 0, s.m_h - 1 })
                if (!s.toRaw(x, y, rx, ry) || !inside(rx, ry)) return false;
        for (int y = 0; y < s.m_h; y += kEdgeStepPx)
            for (const int x : { 0, s.m_w - 1 })
                if (!s.toRaw(x, y, rx, ry) || !inside(rx, ry)) return false;
        return true;
    };
    auto rawShown = [&](double z) {
        setZoom(z);
        double x, y;
        for (int rx = 0; rx < s.m_w; rx += kEdgeStepPx)
            for (const int ry : { 0, s.m_h - 1 })
                if (!s.toStraight(rx, ry, x, y) || !inside(x, y)) return false;
        for (int ry = 0; ry < s.m_h; ry += kEdgeStepPx)
            for (const int rx : { 0, s.m_w - 1 })
                if (!s.toStraight(rx, ry, x, y) || !inside(x, y)) return false;
        return true;
    };
    // The least enlargement that fills it, and the most that shows it all,
    // each found by halving the range where the answer changes; showAll goes
    // between them evenly in proportion.
    auto boundary = [](auto&& tooBig) {   // where tooBig turns true as z grows: (last false, first true)
        double lo = kMinZoom, hi = kMaxZoom;
        for (int i = 0; i < kZoomSteps; ++i) {
            const double mid = std::sqrt(lo * hi);
            (tooBig(mid) ? hi : lo) = mid;
        }
        return std::pair{ lo, hi };
    };
    const double filled = boundary(straightFilled).second;
    const double whole  = boundary([&](double z) { return !rawShown(z); }).first;
    const double a = std::clamp(showAll, 0.0, 1.0);
    setZoom(std::pow(filled, 1 - a) * std::pow(whole, a));

    s.m_grid.resize(size_t(kColumns + 1) * size_t(kRows + 1));
    for (int r = 0; r <= kRows; ++r)
        for (int c = 0; c <= kColumns; ++c) {
            double rx, ry;
            const double x = double(c) * (s.m_w - 1) / kColumns, y = double(r) * (s.m_h - 1) / kRows;
            Node& n = s.m_grid[size_t(r) * (kColumns + 1) + size_t(c)];
            n.seen = s.toRaw(x, y, rx, ry) && inside(rx, ry);
            n.u = float((rx + 0.5) / s.m_w);
            n.v = float((ry + 0.5) / s.m_h);
        }
    return s;
}

bool JPStraightener::toRaw(double x, double y, double& rawX, double& rawY) const {
    // The straightened offset from the middle, as the camera's move relative
    // to what it sees (mm); then through the calibration and the lens.
    const double dx = (x - m_w / 2.0) / m_scaleX, dy = (y - m_h / 2.0) / m_scaleY;
    const double* M = m_cal.pxPerMm.data();
    const double ux = m_u0x + M[0] * dx + M[1] * dy, uy = m_u0y + M[2] * dx + M[3] * dy;
    m_lens.distort(ux, uy, rawX, rawY);
    double bx, by;
    m_lens.undistort(rawX, rawY, bx, by);
    return std::hypot(bx - ux, by - uy) < kRoundTripPx;
}

bool JPStraightener::toStraight(double rawX, double rawY, double& x, double& y) const {
    double ux, uy;
    m_lens.undistort(rawX, rawY, ux, uy);
    const double* M = m_cal.pxPerMm.data();
    const double det = M[0] * M[3] - M[1] * M[2];
    const double ex = ux - m_u0x, ey = uy - m_u0y;
    const double dx = (M[3] * ex - M[1] * ey) / det, dy = (-M[2] * ex + M[0] * ey) / det;
    x = m_w / 2.0 + m_scaleX * dx;
    y = m_h / 2.0 + m_scaleY * dy;
    return true;
}

} // inline namespace jf
