// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPCalibrationFit.h"

#include <cmath>

inline namespace jf {

std::optional<JPCalibrationFit::Result> JPCalibrationFit::fit(const std::vector<Sample>& samples) {
    // Two independent fits sharing one design: x = cx + a*dX + b*dY, and
    // y = cy + c*dX + d*dY. Normal equations of [1 dX dY].
    if (samples.size() < 3) return std::nullopt;
    double n = 0, sx = 0, sy = 0, sxx = 0, sxy = 0, syy = 0;
    double tx = 0, txx = 0, txy = 0, ty = 0, tyx = 0, tyy = 0;
    for (const Sample& s : samples) {
        n += 1; sx += s.dxMm; sy += s.dyMm;
        sxx += s.dxMm * s.dxMm; sxy += s.dxMm * s.dyMm; syy += s.dyMm * s.dyMm;
        tx += s.xPx; txx += s.xPx * s.dxMm; txy += s.xPx * s.dyMm;
        ty += s.yPx; tyx += s.yPx * s.dxMm; tyy += s.yPx * s.dyMm;
    }
    // Solve the 3x3 symmetric system [n sx sy; sx sxx sxy; sy sxy syy] p = t by Cramer's rule.
    const double A[3][3] = { { n, sx, sy }, { sx, sxx, sxy }, { sy, sxy, syy } };
    auto det3 = [](const double m[3][3]) {
        return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
             + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    };
    const double D = det3(A);
    if (std::abs(D) < 1e-12) return std::nullopt;
    auto solve = [&](double t0, double t1, double t2, double out[3]) {
        for (int k = 0; k < 3; ++k) {
            double M[3][3];
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 3; ++c) M[r][c] = A[r][c];
            M[0][k] = t0; M[1][k] = t1; M[2][k] = t2;
            out[k] = det3(M) / D;
        }
    };
    double px[3], py[3];
    solve(tx, txx, txy, px);
    solve(ty, tyx, tyy, py);
    Result r;
    r.centreX = px[0];
    r.centreY = py[0];
    r.pxPerMm = { px[1], px[2], py[1], py[2] };
    double e2 = 0;
    for (const Sample& s : samples) {
        const double ex = r.centreX + px[1] * s.dxMm + px[2] * s.dyMm - s.xPx;
        const double ey = r.centreY + py[1] * s.dxMm + py[2] * s.dyMm - s.yPx;
        e2 += ex * ex + ey * ey;
    }
    r.rmsPx = std::sqrt(e2 / n);
    return r;
}

} // inline namespace jf
