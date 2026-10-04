// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPPartFinder.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

inline namespace jf {

namespace {

// Round the shape, this share of its size more is drawn (the dark round the pads).
constexpr double kMarginShare = 0.25;
constexpr double kLeastMarginMm = 0.3;
// A pattern with less spread than this has nothing to find.
constexpr double kFlat = 1e-6;
// Samples a pattern pixel, each way.
constexpr int kSamples = 2;

struct Pattern {
    int                 size = 0;    // square, odd
    std::vector<double> values;      // mean taken off
    double              norm = 0;    // root sum of squares
};

// The local map from a pixel step to millimetres: m = J p.
struct Jacobian {
    double a = 0, b = 0, c = 0, d = 0;
};

bool inShape(const std::vector<JPPartFinder::Rect>& shape, double x, double y) {
    for (const auto& r : shape) {
        const double t = -r.rotation * M_PI / 180;
        const double dx = x - r.x, dy = y - r.y;
        const double u = dx * std::cos(t) - dy * std::sin(t), v = dx * std::sin(t) + dy * std::cos(t);
        if (std::abs(u) <= r.width / 2 && std::abs(v) <= r.height / 2) return true;
    }
    return false;
}

// The shape at `angle` as `factor`-times-halved pixels see it, centred.
Pattern draw(const std::vector<JPPartFinder::Rect>& shape, const Jacobian& j, double angle, int half, int factor) {
    Pattern p;
    p.size = 2 * half + 1;
    p.values.resize(size_t(p.size) * size_t(p.size));
    const double t = -angle * M_PI / 180, ct = std::cos(t), st = std::sin(t);
    double sum = 0;
    for (int y = 0; y < p.size; ++y)
        for (int x = 0; x < p.size; ++x) {
            int hits = 0;
            for (int sy = 0; sy < kSamples; ++sy)
                for (int sx = 0; sx < kSamples; ++sx) {
                    const double px = (x - half + (sx + 0.5) / kSamples - 0.5) * factor;
                    const double py = (y - half + (sy + 0.5) / kSamples - 0.5) * factor;
                    const double mx = j.a * px + j.b * py, my = j.c * px + j.d * py;
                    hits += inShape(shape, mx * ct - my * st, mx * st + my * ct);
                }
            const double v = double(hits) / (kSamples * kSamples);
            p.values[size_t(y) * size_t(p.size) + size_t(x)] = v;
            sum += v;
        }
    const double mean = sum / double(p.values.size());
    double ss = 0;
    for (double& v : p.values) {
        v -= mean;
        ss += v * v;
    }
    p.norm = std::sqrt(ss);
    return p;
}

// The score with the pattern centred at (cx, cy); -2 where it does not fit.
double score(const JPGrayImage& img, const Pattern& p, int cx, int cy) {
    const int half = p.size / 2, ox = cx - half, oy = cy - half;
    if (ox < 0 || oy < 0 || ox + p.size > img.width || oy + p.size > img.height || p.norm <= kFlat) return -2;
    double sum = 0, sq = 0, cross = 0;
    const double n = double(p.size) * p.size;
    for (int y = 0; y < p.size; ++y) {
        const float* row = &img.pixels[size_t(oy + y) * size_t(img.width) + size_t(ox)];
        const double* pat = &p.values[size_t(y) * size_t(p.size)];
        for (int x = 0; x < p.size; ++x) {
            const double v = row[x];
            sum += v;
            sq += v * v;
            cross += v * pat[x];
        }
    }
    const double var = sq - sum * sum / n;
    if (var <= kFlat) return -2;
    return cross / (std::sqrt(var) * p.norm);
}

double peak(double left, double mid, double right) {
    const double d = left - 2 * mid + right;
    return std::abs(d) < 1e-12 ? 0 : std::clamp(0.5 * (left - right) / d, -0.5, 0.5);
}

} // namespace

JPPartFinder::Result JPPartFinder::find(const JPGrayImage& image, const std::vector<Rect>& shape, const Request& rq) {
    Result r;
    if (shape.empty()) {
        r.why = "the part has no pads or body to look for";
        return r;
    }
    // A pixel's step in millimetres, about where the part should be.
    double m0x, m0y, m1x, m1y, m2x, m2y;
    if (!rq.toMachine || !rq.toMachine(rq.expectedX, rq.expectedY, m0x, m0y) ||
        !rq.toMachine(rq.expectedX + 1, rq.expectedY, m1x, m1y) || !rq.toMachine(rq.expectedX, rq.expectedY + 1, m2x, m2y)) {
        r.why = "the camera's calibration cannot place the picture";
        return r;
    }
    const Jacobian j { m1x - m0x, m2x - m0x, m1y - m0y, m2y - m0y };
    const double mmPerPx = std::sqrt(std::abs(j.a * j.d - j.b * j.c));
    if (mmPerPx <= 0) {
        r.why = "the camera's calibration has no scale";
        return r;
    }
    // How big the shape is, and the pattern round it.
    double radius = 0;
    for (const auto& s : shape) radius = std::max(radius, std::hypot(std::abs(s.x) + s.width / 2, std::abs(s.y) + s.height / 2));
    radius += std::max(kLeastMarginMm, radius * kMarginShare);
    const int halfPx = int(std::ceil(radius / mmPerPx));
    const int searchPx = std::max(1, int(std::ceil(rq.searchMm / mmPerPx)));
    // Coarse: halved until the pattern is small enough to slide quickly.
    int factor = 1;
    JPGrayImage coarse = image;
    while (halfPx / factor > 24 && coarse.width > 64 && coarse.height > 64) {
        coarse = coarse.halved();
        factor *= 2;
    }
    const int cHalf = std::max(2, halfPx / factor), cSearch = std::max(1, searchPx / factor);
    const double range = std::clamp(rq.angleRange, 0.0, 180.0);
    const double step = std::clamp(180 / M_PI * factor / std::max(1.0, radius / mmPerPx), 0.5, 5.0);
    const int cex = int(std::lround(rq.expectedX / factor)), cey = int(std::lround(rq.expectedY / factor));
    double bestScore = -3, bestAngle = rq.angle;
    int bx = cex, by = cey;
    for (double da = -range; da <= range + 1e-9; da += step) {
        const double angle = rq.angle + da;
        const Pattern p = draw(shape, j, angle, cHalf, factor);
        for (int y = cey - cSearch; y <= cey + cSearch; ++y)
            for (int x = cex - cSearch; x <= cex + cSearch; ++x) {
                if ((x - cex) * (x - cex) + (y - cey) * (y - cey) > cSearch * cSearch) continue;
                const double s = score(coarse, p, x, y);
                // Equal scores (a symmetric part): the angle nearest the one expected.
                if (s > bestScore + 1e-9 || (std::abs(s - bestScore) <= 1e-9 && std::abs(da) < std::abs(bestAngle - rq.angle))) {
                    bestScore = s;
                    bestAngle = angle;
                    bx = x;
                    by = y;
                }
            }
    }
    if (bestScore < -1) {
        r.why = "nowhere to look: the place searched is off the picture";
        return r;
    }
    // Fine: the full picture, round the coarse best, the angle to a tenth of the step.
    int fx = bx * factor, fy = by * factor;
    double fineAngle = bestAngle, fineScore = -3;
    for (double da = -step; da <= step + 1e-9; da += step / 4) {
        const Pattern p = draw(shape, j, bestAngle + da, halfPx, 1);
        int lx = fx, ly = fy;
        double top = -3;
        for (int y = by * factor - factor - 1; y <= by * factor + factor + 1; ++y)
            for (int x = bx * factor - factor - 1; x <= bx * factor + factor + 1; ++x) {
                const double s = score(image, p, x, y);
                if (s > top) {
                    top = s;
                    lx = x;
                    ly = y;
                }
            }
        if (top > fineScore) {
            fineScore = top;
            fineAngle = bestAngle + da;
            fx = lx;
            fy = ly;
        }
    }
    // To a fraction of a pixel: a parabola through the neighbours' scores.
    const Pattern p = draw(shape, j, fineAngle, halfPx, 1);
    const double s0 = score(image, p, fx, fy);
    const double sx = peak(score(image, p, fx - 1, fy), s0, score(image, p, fx + 1, fy));
    const double sy = peak(score(image, p, fx, fy - 1), s0, score(image, p, fx, fy + 1));
    r.score = s0;
    r.x = fx + sx;
    r.y = fy + sy;
    r.angle = fineAngle;
    if (std::hypot(r.x - rq.expectedX, r.y - rq.expectedY) > searchPx + factor + 1) {
        r.why = "found only beyond where it was looked for";
        return r;
    }
    if (s0 < rq.minScore) {
        char buf[96];
        std::snprintf(buf, sizeof buf, "nothing like it there (best match %.2f of 1, %.2f needed)", s0, rq.minScore);
        r.why = buf;
        return r;
    }
    r.found = true;
    return r;
}

} // inline namespace jf
