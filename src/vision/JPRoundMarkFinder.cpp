// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPRoundMarkFinder.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

inline namespace jf {

namespace {

constexpr double kTwoPi = 6.283185307179586;

// Ring sampling: rings out to this multiple of the mark's radius (the mark,
// its edge, and some ground around it), sampled this densely.
constexpr double kRingsTo        = 1.5;
constexpr double kRingStepPx     = 1.0;
constexpr int    kMinRingSamples = 16;

// Each ring's mean and the spread of its samples about that mean.
struct Ring { double mean = 0, variance = 0; bool ok = false; };

Ring ring(const JPGrayImage& img, double cx, double cy, double r) {
    const int n = std::max(kMinRingSamples, int(kTwoPi * r));
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

// Refine a peak to a fraction of a step by a parabola through it and its neighbours.
double parabola(double left, double centre, double right) {
    const double d = left - 2 * centre + right;
    return d < 0 ? 0.5 * (left - right) / d : 0;
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

    // Coarse: every other pixel in the search circle; then every pixel around the best.
    auto best = [&](double cx0, double cy0, double radius, double step, double& bx, double& by) {
        double bestScore = -1;
        for (double dy = -radius; dy <= radius; dy += step)
            for (double dx = -radius; dx <= radius; dx += step) {
                if (dx * dx + dy * dy > radius * radius) continue;
                const double s = symmetryAt(image, cx0 + dx, cy0 + dy, maxRadius);
                if (s > bestScore) { bestScore = s; bx = cx0 + dx; by = cy0 + dy; }
            }
        return bestScore;
    };
    double bx = rq.expectedX, by = rq.expectedY;
    double score = best(rq.expectedX, rq.expectedY, rq.searchRadius, 2.0, bx, by);
    double fx = bx, fy = by;
    score = best(bx, by, 2.0, 1.0, fx, fy);
    if (score <= 0) {
        out.why = "nothing round near where the mark should be";
        return out;
    }

    // A fraction of a pixel: parabolas through the score either side, in X and in Y.
    const double cx = fx + parabola(symmetryAt(image, fx - 1, fy, maxRadius), score, symmetryAt(image, fx + 1, fy, maxRadius));
    const double cy = fy + parabola(symmetryAt(image, fx, fy - 1, maxRadius), score, symmetryAt(image, fx, fy + 1, maxRadius));

    // The edge: where ring brightness changes fastest going outward.
    double edgeR = 0, steepest = 0, prev = 0;
    bool havePrev = false;
    for (double r = kRingStepPx; r <= maxRadius; r += kRingStepPx / 2) {
        const Ring rg = ring(image, cx, cy, r);
        if (!rg.ok) break;
        if (havePrev) {
            const double slope = std::abs(rg.mean - prev);
            if (slope > steepest) { steepest = slope; edgeR = r - kRingStepPx / 4; }
        }
        prev = rg.mean;
        havePrev = true;
    }

    out.x = cx;
    out.y = cy;
    out.diameter = 2 * edgeR;
    out.symmetry = score;
    out.shape = shapeAt(image, cx, cy, rq.diameter);
    const double sizeError = out.diameter / rq.diameter - 1;
    out.confidence = std::max(0.0, 1.0 - std::abs(sizeError) / rq.sizeTolerance);
    if (std::abs(sizeError) > rq.sizeTolerance) {
        char buf[160];
        std::snprintf(buf, sizeof buf, "the roundest thing near there measures %.1f px across, not the %.1f px expected",
                      out.diameter, rq.diameter);
        out.why = buf;
        return out;
    }
    if (out.shape < rq.minShape) {
        char buf[160];
        std::snprintf(buf, sizeof buf, "the roundest thing near there is not a whole mark of that size "
                      "(it matches a %.1f px disc by %.0f%%)", rq.diameter, out.shape * 100);
        out.why = buf;
        return out;
    }
    out.confidence = std::min(out.confidence, out.shape);
    out.found = true;
    return out;
}

} // inline namespace jf
