// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPTemplateFinder.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

inline namespace jf {

namespace {

// Halved while the template is wider or higher than this (pixels).
constexpr int kCoarseSide = 24;
// A template with less spread than this has nothing to find.
constexpr double kFlat = 1e-6;
// Around the coarse best, this many full pixels each way are tried.
constexpr int kRefine = 3;

struct Box {
    int x0, y0, x1, y1;   // top left corners tried, inclusive
};

// The template with its mean taken off, and its root sum of squares.
struct Prepared {
    std::vector<double> values;
    double              norm = 0;
};

Prepared prepare(const JPGrayImage& t) {
    Prepared p;
    double mean = 0;
    for (float v : t.pixels) mean += v;
    mean /= double(t.pixels.size());
    p.values.resize(t.pixels.size());
    for (size_t i = 0; i < t.pixels.size(); ++i) {
        p.values[i] = t.pixels[i] - mean;
        p.norm += p.values[i] * p.values[i];
    }
    p.norm = std::sqrt(p.norm);
    return p;
}

double score(const JPGrayImage& img, const JPGrayImage& t, const Prepared& p, int ox, int oy) {
    double sum = 0, sumSq = 0, cross = 0;
    const double n = double(t.width) * double(t.height);
    for (int y = 0; y < t.height; ++y) {
        const float* row = &img.pixels[size_t(oy + y) * size_t(img.width) + size_t(ox)];
        const double* tv = &p.values[size_t(y) * size_t(t.width)];
        for (int x = 0; x < t.width; ++x) {
            const double v = row[x];
            sum += v;
            sumSq += v * v;
            cross += v * tv[x];
        }
    }
    const double spread = std::sqrt(std::max(0.0, sumSq - sum * sum / n));
    if (spread < kFlat) return 0;
    return cross / (spread * p.norm);
}

// The best place in `box`, and its score.
double best(const JPGrayImage& img, const JPGrayImage& t, const Prepared& p, const Box& box, int& bx, int& by) {
    double top = -2;
    for (int y = box.y0; y <= box.y1; ++y)
        for (int x = box.x0; x <= box.x1; ++x) {
            const double s = score(img, t, p, x, y);
            if (s > top) {
                top = s;
                bx = x;
                by = y;
            }
        }
    return top;
}

// A parabola through three scores: how far from the middle one its peak is.
double peak(double a, double b, double c) {
    const double d = a - 2 * b + c;
    return d < 0 ? std::clamp(0.5 * (a - c) / d, -0.5, 0.5) : 0;
}

} // namespace

JPTemplateFinder::Result JPTemplateFinder::find(const JPGrayImage& image, const JPGrayImage& templ, const Area& area,
                                                double minScore) {
    Result r;
    if (templ.width <= 0 || templ.height <= 0) {
        r.why = "no template image";
        return r;
    }
    Area a = area;
    if (a.width <= 0 || a.height <= 0) a = { 0, 0, image.width, image.height };
    const int ax0 = std::clamp(a.x, 0, image.width), ay0 = std::clamp(a.y, 0, image.height);
    const int ax1 = std::clamp(a.x + a.width, 0, image.width), ay1 = std::clamp(a.y + a.height, 0, image.height);
    if (ax1 - ax0 < templ.width || ay1 - ay0 < templ.height) {
        r.why = "the template image is larger than the area of interest";
        return r;
    }
    const Prepared full = prepare(templ);
    if (full.norm < kFlat) {
        r.why = "the template image is all one shade";
        return r;
    }
    const Box whole { ax0, ay0, ax1 - templ.width, ay1 - templ.height };

    // Coarse: both halved alike while the template is large.
    int factor = 1;
    JPGrayImage ci = image, ct = templ;
    while (ct.width > kCoarseSide && ct.height > kCoarseSide) {
        ci = ci.halved();
        ct = ct.halved();
        factor *= 2;
    }
    int bx = whole.x0, by = whole.y0;
    if (factor > 1) {
        const Prepared coarse = prepare(ct);
        const Box box { whole.x0 / factor, whole.y0 / factor, std::min(whole.x1 / factor, ci.width - ct.width),
                        std::min(whole.y1 / factor, ci.height - ct.height) };
        int cx = box.x0, cy = box.y0;
        if (coarse.norm >= kFlat && box.x1 >= box.x0 && box.y1 >= box.y0) best(ci, ct, coarse, box, cx, cy);
        const Box near { std::max(whole.x0, cx * factor - factor - kRefine), std::max(whole.y0, cy * factor - factor - kRefine),
                         std::min(whole.x1, cx * factor + factor + kRefine), std::min(whole.y1, cy * factor + factor + kRefine) };
        r.score = best(image, templ, full, near, bx, by);
    } else {
        r.score = best(image, templ, full, whole, bx, by);
    }
    if (r.score < minScore) {
        char text[96];
        std::snprintf(text, sizeof text, "no match for the template image (best %.2f)", r.score);
        r.why = text;
        return r;
    }
    double fx = 0, fy = 0;
    if (bx > whole.x0 && bx < whole.x1)
        fx = peak(score(image, templ, full, bx - 1, by), r.score, score(image, templ, full, bx + 1, by));
    if (by > whole.y0 && by < whole.y1)
        fy = peak(score(image, templ, full, bx, by - 1), r.score, score(image, templ, full, bx, by + 1));
    r.found = true;
    r.x = bx + fx;
    r.y = by + fy;
    return r;
}

} // inline namespace jf
