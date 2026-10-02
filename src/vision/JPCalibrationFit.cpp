// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPCalibrationFit.h"

#include "common/JPLens.h"

#include <algorithm>
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

namespace {

constexpr int kLensParams = 9;           // centre x, y; M (4); k1; the lens's centre x, y
constexpr int kLensIterations = 30;
constexpr double kLensConverged = 1e-10; // a step this small (relative) ends it
constexpr double kDerivativeStep = 1e-6;

// Solve A x = b (n x n) by elimination with partial pivoting; false when singular.
bool solveLinear(std::vector<double> A, std::vector<double> b, int n, std::vector<double>& x) {
    for (int c = 0; c < n; ++c) {
        int piv = c;
        for (int r = c + 1; r < n; ++r)
            if (std::abs(A[size_t(r * n + c)]) > std::abs(A[size_t(piv * n + c)])) piv = r;
        if (std::abs(A[size_t(piv * n + c)]) < 1e-300) return false;
        if (piv != c) {
            for (int k = 0; k < n; ++k) std::swap(A[size_t(c * n + k)], A[size_t(piv * n + k)]);
            std::swap(b[size_t(c)], b[size_t(piv)]);
        }
        for (int r = c + 1; r < n; ++r) {
            const double f = A[size_t(r * n + c)] / A[size_t(c * n + c)];
            for (int k = c; k < n; ++k) A[size_t(r * n + k)] -= f * A[size_t(c * n + k)];
            b[size_t(r)] -= f * b[size_t(c)];
        }
    }
    x.assign(size_t(n), 0);
    for (int r = n - 1; r >= 0; --r) {
        double v = b[size_t(r)];
        for (int k = r + 1; k < n; ++k) v -= A[size_t(r * n + k)] * x[size_t(k)];
        x[size_t(r)] = v / A[size_t(r * n + r)];
    }
    return true;
}

// Residuals (seen minus modelled), x then y for each sample.
void lensResiduals(const std::vector<JPCalibrationFit::Sample>& samples, const double p[kLensParams],
                   int width, int height, std::vector<double>& out) {
    const JPLens lens = JPLens::forPicture(width, height, p[6], p[7], p[8]);
    out.resize(samples.size() * 2);
    for (size_t i = 0; i < samples.size(); ++i) {
        const auto& s = samples[i];
        double x, y;
        lens.distort(p[0] + p[2] * s.dxMm + p[3] * s.dyMm, p[1] + p[4] * s.dxMm + p[5] * s.dyMm, x, y);
        out[2 * i]     = s.xPx - x;
        out[2 * i + 1] = s.yPx - y;
    }
}

} // namespace

std::optional<JPCalibrationFit::Result> JPCalibrationFit::fitWithLens(const std::vector<Sample>& samples, int width,
                                                                      int height, bool lensCentre) {
    // The lens's centre held at the picture's middle is two fewer to fit.
    const int fitted = lensCentre ? kLensParams : kLensParams - 2;
    if (samples.size() * 2 <= size_t(fitted) || width <= 0 || height <= 0) return std::nullopt;
    // From the straight fit; for the lens's centre, from the fit about the
    // middle (with no bending yet, where it bends about changes nothing).
    const auto start = lensCentre ? fitWithLens(samples, width, height, false) : fit(samples);
    if (!start) return std::nullopt;
    double p[kLensParams] = { start->centreX, start->centreY, start->pxPerMm[0], start->pxPerMm[1],
                              start->pxPerMm[2], start->pxPerMm[3], start->lensK1, width / 2.0, height / 2.0 };
    std::vector<double> r, rd, J(samples.size() * 2 * size_t(fitted)), step;
    for (int it = 0; it < kLensIterations; ++it) {
        lensResiduals(samples, p, width, height, r);
        // The Jacobian by differences: the model is smooth and small.
        const size_t n = size_t(fitted);
        for (int k = 0; k < fitted; ++k) {
            const double h = kDerivativeStep * std::max(1.0, std::abs(p[k]));
            double q[kLensParams];
            std::copy(p, p + kLensParams, q);
            q[k] += h;
            lensResiduals(samples, q, width, height, rd);
            for (size_t i = 0; i < r.size(); ++i) J[i * n + size_t(k)] = (rd[i] - r[i]) / h;
        }
        // Normal equations: (J^T J) step = -J^T r.
        std::vector<double> A(n * n, 0), b(n, 0);
        for (size_t i = 0; i < r.size(); ++i)
            for (size_t a = 0; a < n; ++a) {
                b[a] -= J[i * n + a] * r[i];
                for (size_t c = 0; c < n; ++c) A[a * n + c] += J[i * n + a] * J[i * n + c];
            }
        if (!solveLinear(A, b, fitted, step)) return std::nullopt;
        double change = 0, size = 0;
        for (int k = 0; k < fitted; ++k) {
            p[k] += step[size_t(k)];
            change += step[size_t(k)] * step[size_t(k)];
            size += p[k] * p[k];
        }
        if (change <= kLensConverged * kLensConverged * size) break;
    }
    lensResiduals(samples, p, width, height, r);
    Result out;
    out.centreX = p[0];
    out.centreY = p[1];
    out.pxPerMm = { p[2], p[3], p[4], p[5] };
    out.lensK1  = p[6];
    out.lensCentreX = p[7];
    out.lensCentreY = p[8];
    double e2 = 0;
    for (double v : r) e2 += v * v;
    out.rmsPx = std::sqrt(e2 / double(samples.size()));
    return out;
}

} // inline namespace jf
