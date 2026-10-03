// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPRunout.h"

#include <cmath>

inline namespace jf {

namespace {

constexpr double kRad = M_PI / 180;

} // namespace

void JPRunout::runoutAt(double a, double& dx, double& dy) const {
    const double t = (a - phaseDeg) * kRad;
    dx = radius * std::cos(t);
    dy = radius * std::sin(t);
}

std::optional<JPRunout> JPRunout::fit(const std::vector<Point>& points) {
    if (points.size() < 3) return std::nullopt;
    // Unknowns u = (cx, cy, A, B), A = r cos phase, B = r sin phase:
    //   dx = cx + A cos a + B sin a,   dy = cy + A sin a - B cos a.
    // Normal equations N u = v.
    double N[4][4] = {}, v[4] = {};
    auto row = [&](const double r[4], double y) {
        for (int i = 0; i < 4; ++i) {
            v[i] += r[i] * y;
            for (int j = 0; j < 4; ++j) N[i][j] += r[i] * r[j];
        }
    };
    for (const Point& p : points) {
        const double c = std::cos(p.angle * kRad), s = std::sin(p.angle * kRad);
        const double rx[4] = { 1, 0, c, s }, ry[4] = { 0, 1, s, -c };
        row(rx, p.dx);
        row(ry, p.dy);
    }
    // Gaussian elimination with partial pivoting.
    double u[4];
    for (int k = 0; k < 4; ++k) {
        int best = k;
        for (int i = k + 1; i < 4; ++i)
            if (std::abs(N[i][k]) > std::abs(N[best][k])) best = i;
        if (std::abs(N[best][k]) < 1e-12) return std::nullopt;
        if (best != k) {
            for (int j = 0; j < 4; ++j) std::swap(N[k][j], N[best][j]);
            std::swap(v[k], v[best]);
        }
        for (int i = k + 1; i < 4; ++i) {
            const double f = N[i][k] / N[k][k];
            for (int j = k; j < 4; ++j) N[i][j] -= f * N[k][j];
            v[i] -= f * v[k];
        }
    }
    for (int k = 3; k >= 0; --k) {
        double sum = v[k];
        for (int j = k + 1; j < 4; ++j) sum -= N[k][j] * u[j];
        u[k] = sum / N[k][k];
    }
    JPRunout r;
    r.centreX = u[0];
    r.centreY = u[1];
    r.radius = std::hypot(u[2], u[3]);
    r.phaseDeg = std::atan2(u[3], u[2]) / kRad;
    r.points = points;
    double sum = 0;
    for (const Point& p : points) {
        double sx, sy;
        r.runoutAt(p.angle, sx, sy);
        const double e = std::hypot(p.dx - r.centreX - sx, p.dy - r.centreY - sy);
        sum += e * e;
        r.peakMm = std::max(r.peakMm, e);
    }
    r.rmsMm = std::sqrt(sum / double(points.size()));
    return r;
}

JPRunout JPRunout::fromJson(const JJson& j) {
    JPRunout r;
    r.centreX = j["centre"]["x"].number();
    r.centreY = j["centre"]["y"].number();
    r.radius = j["radius"].number();
    r.phaseDeg = j["phase"].number();
    r.rmsMm = j["rms"].number();
    r.peakMm = j["peak"].number();
    r.when = j["when"].str();
    for (const JJson& p : j["points"].arr()) r.points.push_back({ p[0].number(), p[1].number(), p[2].number() });
    return r;
}

JJson JPRunout::toJson() const {
    JJson j = JJson::object();
    j["centre"]["x"] = centreX;
    j["centre"]["y"] = centreY;
    j["radius"] = radius;
    j["phase"] = phaseDeg;
    j["rms"] = rmsMm;
    j["peak"] = peakMm;
    j["when"] = when;
    j["points"] = JJson::array();
    for (const Point& p : points) {
        JJson q = JJson::array();
        q.push(JJson(p.angle));
        q.push(JJson(p.dx));
        q.push(JJson(p.dy));
        j["points"].push(q);
    }
    return j;
}

} // inline namespace jf
