// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPPatternFinder.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

inline namespace jf {

namespace {

// How many times the coarse search halves the pictures, and how far round the
// coarse best the full picture is searched (pixels).
constexpr int kCoarseHalvings = 2;
constexpr int kRefineReach = 6;
// A pattern with less spread than this (grey levels) has nothing to find.
constexpr double kFlat = 1e-3;

struct Prepared {
    const JPGrayImage* image;
    int                w, h;
    double             mean, norm;   // the pattern's mean, and its root sum of squared deviations
};

Prepared prepare(const JPGrayImage& p) {
    double sum = 0;
    for (const float v : p.pixels) sum += v;
    const double mean = p.pixels.empty() ? 0 : sum / double(p.pixels.size());
    double ss = 0;
    for (const float v : p.pixels) ss += (v - mean) * (v - mean);
    return { &p, p.width, p.height, mean, std::sqrt(ss) };
}

// The score with the pattern's top-left at (ox, oy) in the image; -2 where it
// does not fit.
double score(const JPGrayImage& img, const Prepared& t, int ox, int oy) {
    if (ox < 0 || oy < 0 || ox + t.w > img.width || oy + t.h > img.height) return -2;
    double sum = 0, sq = 0, cross = 0;
    const size_t n = size_t(t.w) * size_t(t.h);
    for (int y = 0; y < t.h; ++y) {
        const float* row = &img.pixels[size_t(oy + y) * size_t(img.width) + size_t(ox)];
        const float* pat = &t.image->pixels[size_t(y) * size_t(t.w)];
        for (int x = 0; x < t.w; ++x) {
            const double v = row[x];
            sum += v;
            sq += v * v;
            cross += v * (pat[x] - t.mean);
        }
    }
    const double var = sq - sum * sum / double(n);
    if (var <= kFlat || t.norm <= kFlat) return -2;
    return cross / (std::sqrt(var) * t.norm);
}

// The best score within `reach` of (cx, cy) (top-left positions), its place.
double best(const JPGrayImage& img, const Prepared& t, int cx, int cy, int reach, int& bx, int& by) {
    double top = -3;
    for (int y = cy - reach; y <= cy + reach; ++y)
        for (int x = cx - reach; x <= cx + reach; ++x) {
            if ((x - cx) * (x - cx) + (y - cy) * (y - cy) > reach * reach) continue;
            const double s = score(img, t, x, y);
            if (s > top) {
                top = s;
                bx = x;
                by = y;
            }
        }
    return top;
}

// The peak of a parabola through three scores either side of the middle one.
double peak(double left, double mid, double right) {
    const double d = left - 2 * mid + right;
    return std::abs(d) < 1e-12 ? 0 : std::clamp(0.5 * (left - right) / d, -0.5, 0.5);
}

} // namespace

JPPatternFinder::Result JPPatternFinder::find(const JPGrayImage& image, const JPGrayImage& pattern, double centreX,
                                              double centreY, const Request& rq) {
    Result r;
    if (pattern.width < 4 || pattern.height < 4 || pattern.width > image.width || pattern.height > image.height) {
        r.why = "the pattern does not fit in the picture";
        return r;
    }
    // Coarse: both halved, searched over the whole radius.
    JPGrayImage ci = image, cp = pattern;
    int factor = 1;
    for (int i = 0; i < kCoarseHalvings && cp.width >= 16 && cp.height >= 16; ++i) {
        ci = ci.halved();
        cp = cp.halved();
        factor *= 2;
    }
    const Prepared coarse = prepare(cp);
    if (coarse.norm <= kFlat) {
        r.why = "the pattern is all one shade";
        return r;
    }
    const int ccx = int(std::lround((rq.expectedX - centreX) / factor));
    const int ccy = int(std::lround((rq.expectedY - centreY) / factor));
    int bx = ccx, by = ccy;
    const int reach = std::max(1, int(std::ceil(rq.searchRadius / factor)));
    if (best(ci, coarse, ccx, ccy, reach, bx, by) < -1) {
        r.why = "nowhere to look: the place searched is off the picture";
        return r;
    }
    // Fine: the full pictures, close round the coarse best.
    const Prepared fine = prepare(pattern);
    int fx = bx * factor, fy = by * factor;
    const double s = best(image, fine, fx, fy, kRefineReach + factor, fx, fy);
    r.score = s;
    // Still within the radius asked for.
    const double dx = fx + centreX - rq.expectedX, dy = fy + centreY - rq.expectedY;
    if (std::hypot(dx, dy) > rq.searchRadius + 1) {
        r.why = "found only beyond where it was looked for";
        return r;
    }
    if (s < rq.minScore) {
        char buf[96];
        std::snprintf(buf, sizeof buf, "nothing like it there (best match %.2f of 1, %.2f needed)", s, rq.minScore);
        r.why = buf;
        return r;
    }
    const double ox = peak(score(image, fine, fx - 1, fy), s, score(image, fine, fx + 1, fy));
    const double oy = peak(score(image, fine, fx, fy - 1), s, score(image, fine, fx, fy + 1));
    r.found = true;
    r.x = fx + ox + centreX;
    r.y = fy + oy + centreY;
    return r;
}

} // inline namespace jf
