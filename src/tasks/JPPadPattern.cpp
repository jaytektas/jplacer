// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPPadPattern.h"

#include <algorithm>
#include <cmath>

inline namespace jf {

namespace {

// Round a footprint's pads, what its pattern shows of the board (mm).
constexpr double kPatternMarginMm = 0.3;
constexpr double kPi = 3.14159265358979323846;

} // namespace

bool JPPadPattern::draw(const JPCameraCalibration& cal, double lookX, double lookY, const JPAffine2D& board,
                        const JPPlacement& p, const JPFootprint& f, double degrees, double ex, double ey, JPGrayImage& out,
                        double& cx, double& cy) {
    const std::optional<JPAffine2D> toBoard = board.inverse();
    if (!toBoard || f.pads.empty()) return false;
    double reach = 0;
    for (const JPPad& pad : f.pads) reach = std::max(reach, std::hypot(std::abs(pad.x) + pad.width / 2, std::abs(pad.y) + pad.height / 2));
    const double scale = std::sqrt(cal.scaleX() * cal.scaleY());
    const int half = int(std::ceil((reach + kPatternMarginMm) * scale));
    out.width = out.height = 2 * half + 1;
    out.pixels.assign(size_t(out.width) * size_t(out.height), 0.f);
    const double a = degrees * kPi / 180, c = std::cos(a), s = std::sin(a);
    const bool bottom = p.side == JPPlacement::Side::Bottom;
    for (int j = 0; j < out.height; ++j)
        for (int i = 0; i < out.width; ++i) {
            int lit = 0;
            for (int sj = 0; sj < 2; ++sj)
                for (int si = 0; si < 2; ++si) {
                    const double u = ex - half + i + (si + 0.5) / 2 - 0.5, v = ey - half + j + (sj + 0.5) / 2 - 0.5;
                    double mx, my, bx, by;
                    if (!cal.machinePoint(u, v, lookX, lookY, mx, my)) continue;
                    toBoard->apply(mx, my, bx, by);
                    const double dx = bx - p.x, dy = by - p.y;
                    double lx = dx * c + dy * s;
                    const double ly = -dx * s + dy * c;
                    if (bottom) lx = -lx;   // seen through the board
                    for (const JPPad& pad : f.pads) {
                        const double pa = pad.rotationDeg * kPi / 180, pc = std::cos(pa), ps = std::sin(pa);
                        const double qx = (lx - pad.x) * pc + (ly - pad.y) * ps, qy = -(lx - pad.x) * ps + (ly - pad.y) * pc;
                        if (std::abs(qx) <= pad.width / 2 && std::abs(qy) <= pad.height / 2) {
                            ++lit;
                            break;
                        }
                    }
                }
            out.pixels[size_t(j) * size_t(out.width) + size_t(i)] = float(lit) * 255.f / 4.f;
        }
    cx = cy = half;
    return true;
}


} // inline namespace jf
