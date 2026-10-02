// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPPointFit.h"

#include <cmath>

inline namespace jf {

namespace {

double rmsOf(const std::vector<JPPointFit::Pair>& pairs, const JPAffine2D& m) {
    double e2 = 0;
    for (const auto& p : pairs) {
        double x, y;
        m.apply(p.fromX, p.fromY, x, y);
        e2 += (x - p.toX) * (x - p.toX) + (y - p.toY) * (y - p.toY);
    }
    return std::sqrt(e2 / double(pairs.size()));
}

} // namespace

std::optional<JPPointFit::Result> JPPointFit::rigid(const std::vector<Pair>& pairs, const JPAffine2D& base) {
    if (pairs.empty()) return std::nullopt;
    // Through base first; then the turn and move that best fit (Procrustes).
    std::vector<Pair> through = pairs;
    for (Pair& p : through) base.apply(p.fromX, p.fromY, p.fromX, p.fromY);
    double fx = 0, fy = 0, tx = 0, ty = 0;
    for (const Pair& p : through) { fx += p.fromX; fy += p.fromY; tx += p.toX; ty += p.toY; }
    const double n = double(through.size());
    fx /= n; fy /= n; tx /= n; ty /= n;
    double angle = 0;
    if (through.size() >= 2) {
        double sc = 0, ss = 0;
        for (const Pair& p : through) {
            const double ux = p.fromX - fx, uy = p.fromY - fy, vx = p.toX - tx, vy = p.toY - ty;
            sc += ux * vx + uy * vy;
            ss += ux * vy - uy * vx;
        }
        if (sc == 0 && ss == 0) return std::nullopt;
        angle = std::atan2(ss, sc);
    }
    const double cs = std::cos(angle), sn = std::sin(angle);
    JPAffine2D turn;
    turn.a = cs;
    turn.b = -sn;
    turn.c = sn;
    turn.d = cs;
    turn.tx = tx - (cs * fx - sn * fy);
    turn.ty = ty - (sn * fx + cs * fy);
    Result r;
    r.map = turn.after(base);
    r.rms = rmsOf(pairs, r.map);
    return r;
}

std::optional<JPPointFit::Result> JPPointFit::affine(const std::vector<Pair>& pairs) {
    if (pairs.size() < 3) return std::nullopt;
    // Two least-squares fits sharing one design [x y 1], solved about the mean.
    double mx = 0, my = 0;
    for (const Pair& p : pairs) { mx += p.fromX; my += p.fromY; }
    const double n = double(pairs.size());
    mx /= n;
    my /= n;
    double sxx = 0, sxy = 0, syy = 0, txx = 0, txy = 0, tyx = 0, tyy = 0, tx = 0, ty = 0;
    for (const Pair& p : pairs) {
        const double u = p.fromX - mx, v = p.fromY - my;
        sxx += u * u; sxy += u * v; syy += v * v;
        txx += p.toX * u; txy += p.toX * v; tyx += p.toY * u; tyy += p.toY * v;
        tx += p.toX; ty += p.toY;
    }
    const double det = sxx * syy - sxy * sxy;
    if (std::abs(det) < 1e-12 * (sxx + syy) * (sxx + syy) || det <= 0) return std::nullopt;
    JPAffine2D m;
    m.a = (txx * syy - txy * sxy) / det;
    m.b = (sxx * txy - sxy * txx) / det;
    m.c = (tyx * syy - tyy * sxy) / det;
    m.d = (sxx * tyy - sxy * tyx) / det;
    m.tx = tx / n - (m.a * mx + m.b * my);
    m.ty = ty / n - (m.c * mx + m.d * my);
    Result r;
    r.map = m;
    r.rms = rmsOf(pairs, m);
    return r;
}

} // inline namespace jf
