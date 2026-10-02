// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPRoundMarkFinder.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <future>
#include <vector>

inline namespace jf {

namespace {

constexpr double kTwoPi = 6.283185307179586;

// Ring sampling: rings out to this multiple of the mark's radius (the mark,
// its edge, and some ground around it), sampled this densely.
constexpr double kRingsTo        = 1.5;
constexpr double kRingStepPx     = 1.0;
constexpr int    kMinRingSamples = 16;
constexpr int    kMaxRingSamples = 64;    // more says no more about how even a ring is
// The mark's size in the coarse copy the search runs on (pixels across).
constexpr double kCoarseDiameter = 6;

// Each ring's mean and the spread of its samples about that mean.
struct Ring { double mean = 0, variance = 0; bool ok = false; };

Ring ring(const JPGrayImage& img, double cx, double cy, double r) {
    const int n = std::clamp(int(kTwoPi * r), kMinRingSamples, kMaxRingSamples);
    double sum = 0, sum2 = 0;
    int count = 0;
    for (int i = 0; i < n; ++i) {
        const double a = kTwoPi * i / n;
        float v;
        if (!img.sample(float(cx + r * std::cos(a)), float(cy + r * std::sin(a)), v)) continue;
        sum += v;
        sum2 += double(v) * v;
        ++count;
    }
    Ring out;
    if (count < n * 3 / 4) return out;   // mostly off the picture
    out.mean = sum / count;
    out.variance = std::max(0.0, sum2 / count - out.mean * out.mean);
    out.ok = true;
    return out;
}

// The edge measurement: rays out from the centre found, each finding where it
// crosses halfway between the inside's brightness and the outside's (its own
// levels, so light that is brighter on one side moves nothing), to a fraction
// of a pixel; a circle is fitted through the crossings.
constexpr int    kEdgeRays       = 72;
constexpr double kRayStepPx      = 0.25;
constexpr double kInsideFrom     = 0.3;   // the inside's level: this share of the radius...
constexpr double kInsideTo       = 0.6;   // ...to this
constexpr double kOutsideFrom    = 1.3;   // the outside's: this...
constexpr double kOutsideTo      = 1.5;   // ...to this (kRingsTo)
// A ray whose inside and outside differ by less than this share of the
// typical ray's is crossing something else (glare, a trace) and is left out.
constexpr double kMinRayContrast = 0.5;
// A crossing further than this many times the fit's spread from the circle is
// left out and the circle fitted again.
constexpr double kOutlierSpread  = 3.0;
constexpr double kMinSpreadPx    = 0.25;

struct Circle { double x = 0, y = 0, r = 0; bool ok = false; };

double meanAlong(const JPGrayImage& img, double cx, double cy, double ux, double uy, double from, double to, bool& ok) {
    double sum = 0;
    int n = 0;
    for (double t = from; t <= to; t += kRayStepPx) {
        float v;
        if (!img.sample(float(cx + t * ux), float(cy + t * uy), v)) { ok = false; return 0; }
        sum += v;
        ++n;
    }
    ok = n > 0;
    return n ? sum / n : 0;
}

// Least squares circle through points (x^2 + y^2 + D x + E y + F = 0).
Circle fitCircle(const std::vector<std::pair<double, double>>& pts) {
    Circle c;
    if (pts.size() < 3) return c;
    double mx = 0, my = 0;
    for (const auto& [x, y] : pts) { mx += x; my += y; }
    mx /= double(pts.size());
    my /= double(pts.size());
    // About the points' mean, for a well-conditioned solve.
    double suu = 0, svv = 0, suv = 0, suuu = 0, svvv = 0, suvv = 0, svuu = 0;
    for (const auto& [x, y] : pts) {
        const double u = x - mx, v = y - my;
        suu += u * u; svv += v * v; suv += u * v;
        suuu += u * u * u; svvv += v * v * v; suvv += u * v * v; svuu += v * u * u;
    }
    const double det = suu * svv - suv * suv;
    if (std::abs(det) < 1e-12) return c;
    const double bu = 0.5 * (suuu + suvv), bv = 0.5 * (svvv + svuu);
    const double uc = (bu * svv - bv * suv) / det, vc = (suu * bv - suv * bu) / det;
    c.x = mx + uc;
    c.y = my + vc;
    c.r = std::sqrt(uc * uc + vc * vc + (suu + svv) / double(pts.size()));
    c.ok = true;
    return c;
}

Circle edgeCircle(const JPGrayImage& img, double cx, double cy, double radius) {
    struct Ray { double x, y, contrast; };
    std::vector<Ray> rays;
    for (int i = 0; i < kEdgeRays; ++i) {
        const double a = kTwoPi * i / kEdgeRays, ux = std::cos(a), uy = std::sin(a);
        bool okIn, okOut;
        const double in  = meanAlong(img, cx, cy, ux, uy, kInsideFrom * radius, kInsideTo * radius, okIn);
        const double out = meanAlong(img, cx, cy, ux, uy, kOutsideFrom * radius, kOutsideTo * radius, okOut);
        if (!okIn || !okOut || in == out) continue;
        const double mid = (in + out) / 2;
        // The crossing nearest the expected edge.
        double bestT = -1, prevT = kInsideTo * radius;
        float prev;
        if (!img.sample(float(cx + prevT * ux), float(cy + prevT * uy), prev)) continue;
        for (double t = prevT + kRayStepPx; t <= kOutsideFrom * radius; t += kRayStepPx) {
            float v;
            if (!img.sample(float(cx + t * ux), float(cy + t * uy), v)) break;
            if ((prev - mid) * (v - mid) <= 0 && prev != v) {
                const double at = prevT + kRayStepPx * (prev - mid) / (prev - v);
                if (bestT < 0 || std::abs(at - radius) < std::abs(bestT - radius)) bestT = at;
            }
            prev = v;
            prevT = t;
        }
        if (bestT > 0) rays.push_back({ cx + bestT * ux, cy + bestT * uy, std::abs(in - out) });
    }
    if (rays.size() < kEdgeRays / 2) return {};
    std::vector<double> contrasts;
    for (const Ray& r : rays) contrasts.push_back(r.contrast);
    std::nth_element(contrasts.begin(), contrasts.begin() + contrasts.size() / 2, contrasts.end());
    const double typical = contrasts[contrasts.size() / 2];
    std::vector<std::pair<double, double>> pts;
    for (const Ray& r : rays)
        if (r.contrast >= kMinRayContrast * typical) pts.push_back({ r.x, r.y });
    if (pts.size() < kEdgeRays / 2) return {};
    Circle c = fitCircle(pts);
    if (!c.ok) return c;
    double spread = 0;
    for (const auto& [x, y] : pts) spread += std::pow(std::hypot(x - c.x, y - c.y) - c.r, 2);
    spread = std::max(kMinSpreadPx, std::sqrt(spread / double(pts.size())));
    std::vector<std::pair<double, double>> kept;
    for (const auto& p : pts)
        if (std::abs(std::hypot(p.first - c.x, p.second - c.y) - c.r) <= kOutlierSpread * spread) kept.push_back(p);
    if (kept.size() < kEdgeRays / 2) return {};
    return fitCircle(kept);
}

} // namespace

double JPRoundMarkFinder::symmetryAt(const JPGrayImage& image, double cx, double cy, double maxRadius) {
    std::vector<double> means;
    double within = 0;
    for (double r = kRingStepPx; r <= maxRadius; r += kRingStepPx) {
        const Ring rg = ring(image, cx, cy, r);
        if (!rg.ok) return 0;
        means.push_back(rg.mean);
        within += rg.variance;
    }
    if (means.size() < 2) return 0;
    double m = 0;
    for (double v : means) m += v;
    m /= double(means.size());
    double between = 0;
    for (double v : means) between += (v - m) * (v - m);
    between /= double(means.size());
    within /= double(means.size());
    // +1: a perfectly flat patch (no mark) scores nothing, not infinity.
    return between / (within + 1.0);
}

JPRoundMark JPRoundMarkFinder::findAnySize(const JPGrayImage& image, double expectedX, double expectedY,
                                           double searchRadius, double minDiameter, double maxDiameter) {
    constexpr double kSizeStep = 1.2;
    // Each size is a search of its own: they run side by side.
    std::vector<std::future<JPRoundMark>> tries;
    for (double d = minDiameter; d <= maxDiameter; d *= kSizeStep)
        tries.push_back(std::async(std::launch::async, [&image, expectedX, expectedY, searchRadius, d] {
            Request rq;
            rq.expectedX = expectedX;
            rq.expectedY = expectedY;
            rq.searchRadius = searchRadius;
            rq.diameter = d;
            return find(image, rq);
        }));
    JPRoundMark best;
    best.why = "nothing round of any size near there";
    for (auto& t : tries) {
        const JPRoundMark m = t.get();
        if (m.found && (!best.found || m.confidence > best.confidence)) best = m;
    }
    return best;
}

double JPRoundMarkFinder::shapeAt(const JPGrayImage& image, double cx, double cy, double diameter) {
    // A disc of the expected size in a square of ground around it, edges
    // anti-aliased over one pixel; correlated with the picture there.
    const double r = diameter / 2, half = kRingsTo * r;
    double st = 0, si = 0, stt = 0, sii = 0, sti = 0;
    int n = 0;
    for (double dy = -half; dy <= half; dy += 1.0)
        for (double dx = -half; dx <= half; dx += 1.0) {
            float v;
            if (!image.sample(float(cx + dx), float(cy + dy), v)) continue;
            const double t = std::clamp(r + 0.5 - std::sqrt(dx * dx + dy * dy), 0.0, 1.0);
            st += t; si += v; stt += t * t; sii += double(v) * v; sti += t * v;
            ++n;
        }
    if (n < 4) return 0;
    const double cov = sti - st * si / n;
    const double vt = stt - st * st / n, vi = sii - si * si / n;
    return (vt > 0 && vi > 0) ? std::abs(cov) / std::sqrt(vt * vi) : 0;
}

JPRoundMark JPRoundMarkFinder::find(const JPGrayImage& image, const Request& rq) {
    JPRoundMark out;
    if (rq.diameter <= 2 || rq.searchRadius < 0) {
        out.why = "the expected size is too small to measure";
        return out;
    }
    const double maxRadius = kRingsTo * rq.diameter / 2;

    // Search where it is cheap, measure where it is exact: the coarse search
    // runs on the picture halved until the mark is about kCoarseDiameter
    // across, every pixel of that; the fine search on the full picture, every
    // pixel within a coarse pixel or two of the best.
    auto best = [](const JPGrayImage& img, double cx0, double cy0, double radius, double maxR, double& bx, double& by) {
        double bestScore = -1;
        for (double dy = -radius; dy <= radius; dy += 1.0)
            for (double dx = -radius; dx <= radius; dx += 1.0) {
                if (dx * dx + dy * dy > radius * radius) continue;
                const double s = symmetryAt(img, cx0 + dx, cy0 + dy, maxR);
                if (s > bestScore) { bestScore = s; bx = cx0 + dx; by = cy0 + dy; }
            }
        return bestScore;
    };
    JPGrayImage coarse;
    const JPGrayImage* level = &image;
    double scale = 1;
    while (rq.diameter / (scale * 2) >= kCoarseDiameter && level->width >= 32 && level->height >= 32) {
        coarse = level->halved();
        level = &coarse;
        scale *= 2;
    }
    double bx = rq.expectedX / scale, by = rq.expectedY / scale;
    best(*level, rq.expectedX / scale, rq.expectedY / scale, rq.searchRadius / scale, maxRadius / scale, bx, by);
    // On whole pixels: a grid that followed the start point's fraction would
    // carry that fraction into the answer.
    double fx = std::round(bx * scale), fy = std::round(by * scale);
    const double score = best(image, fx, fy, scale + 1, maxRadius, fx, fy);
    if (score <= 0) {
        out.why = "nothing round near where the mark should be";
        return out;
    }

    // Its size, roughly: where ring brightness changes fastest going outward.
    double roughR = 0, steepest = 0, prev = 0;
    bool havePrev = false;
    for (double r = kRingStepPx; r <= maxRadius; r += kRingStepPx / 2) {
        const Ring rg = ring(image, fx, fy, r);
        if (!rg.ok) break;
        if (havePrev) {
            const double slope = std::abs(rg.mean - prev);
            if (slope > steepest) { steepest = slope; roughR = r - kRingStepPx / 4; }
        }
        prev = rg.mean;
        havePrev = true;
    }
    out.x = fx;
    out.y = fy;
    out.diameter = 2 * roughR;
    out.symmetry = score;
    auto wrongSize = [&] {
        const double sizeError = out.diameter / rq.diameter - 1;
        out.confidence = std::max(0.0, 1.0 - std::abs(sizeError) / rq.sizeTolerance);
        if (std::abs(sizeError) <= rq.sizeTolerance) return false;
        char buf[160];
        std::snprintf(buf, sizeof buf, "the roundest thing near there measures %.1f px across, not the %.1f px expected",
                      out.diameter, rq.diameter);
        out.why = buf;
        return true;
    };
    // A disc of that size, not the edge of something bigger.
    auto notWhole = [&] {
        out.shape = shapeAt(image, out.x, out.y, rq.diameter);
        if (out.shape >= rq.minShape) return false;
        char buf[160];
        std::snprintf(buf, sizeof buf, "the roundest thing near there is not a whole mark of that size "
                      "(it matches a %.1f px disc by %.0f%%)", rq.diameter, out.shape * 100);
        out.why = buf;
        return true;
    };
    if (wrongSize() || notWhole()) return out;

    // Exactly, to a fraction of a pixel: the edge, measured all round, from
    // the best whole pixel and then again from where that put the centre.
    Circle edge = edgeCircle(image, fx, fy, rq.diameter / 2);
    if (edge.ok) edge = edgeCircle(image, edge.x, edge.y, edge.r);
    if (!edge.ok) {
        out.why = "the round thing near there has no clear edge all round";
        return out;
    }
    const double cx = edge.x, cy = edge.y;
    out.x = cx;
    out.y = cy;
    out.diameter = 2 * edge.r;
    if (wrongSize()) return out;
    if (notWhole()) return out;
    out.confidence = std::min(out.confidence, out.shape);
    out.found = true;
    return out;
}

} // inline namespace jf
