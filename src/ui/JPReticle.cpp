// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPReticle.h"

#include <cmath>
#include <cstdio>
#include <sstream>

inline namespace jf {

namespace {

// A line in millimetres is drawn as this many straight pieces, enough to
// follow the lens's bending.
constexpr int kPieces = 24;
// A circle as this many.
constexpr int kCirclePieces = 96;

const char* word(JPReticle::Kind kind) {
    switch (kind) {
        case JPReticle::Kind::None:   return "none";
        case JPReticle::Kind::Cross:  return "cross";
        case JPReticle::Kind::Grid:   return "grid";
        case JPReticle::Kind::Ruler:  return "ruler";
        case JPReticle::Kind::Circle: return "circle";
        case JPReticle::Kind::Square: return "square";
    }
    return "cross";
}

// The smallest of 1, 2, 5, 10, 20, 50... times `spacing` that is at least
// `gap` on screen.
double thinned(double spacing, double pxPerMm, float gap) {
    static const double steps[] = { 1, 2, 5 };
    for (double decade = 1; decade < 1e6; decade *= 10)
        for (double s : steps)
            if (spacing * s * decade * pxPerMm >= gap) return spacing * s * decade;
    return spacing * 1e6;
}

} // namespace

const std::vector<JPReticle::Kind>& JPReticle::kinds() {
    static const std::vector<Kind> all = { Kind::None, Kind::Cross, Kind::Grid, Kind::Ruler, Kind::Circle, Kind::Square };
    return all;
}

std::string JPReticle::name(Kind kind) {
    switch (kind) {
        case Kind::None:   return "None";
        case Kind::Cross:  return "Cross";
        case Kind::Grid:   return "Grid";
        case Kind::Ruler:  return "Ruler";
        case Kind::Circle: return "Circle";
        case Kind::Square: return "Square";
    }
    return "Cross";
}

const std::vector<double>& JPReticle::spacings() {
    static const std::vector<double> all = { 0.1, 0.25, 0.5, 1, 2, 5, 10 };
    return all;
}

const std::vector<double>& JPReticle::sizes() {
    static const std::vector<double> all = { 0.5, 1, 1.5, 2, 2.5, 3, 4, 5, 10 };
    return all;
}

std::string JPReticle::toText() const {
    char text[64];
    std::snprintf(text, sizeof text, "%s %g %g", word(kind), spacingMm, sizeMm);
    return text;
}

JPReticle JPReticle::fromText(const std::string& text) {
    JPReticle r;
    std::istringstream in(text);
    std::string w;
    double spacing = 0, size = 0;
    if (!(in >> w)) return r;
    for (Kind k : kinds())
        if (w == word(k)) r.kind = k;
    if (in >> spacing && spacing > 0) r.spacingMm = spacing;
    if (in >> size && size > 0) r.sizeMm = size;
    return r;
}

void JPReticle::line(JVectorCanvas& vg, const Place& place, double ax, double ay, double bx, double by, float width,
                     const JPaint& paint) {
    float px = 0, py = 0;
    bool had = false;
    for (int i = 0; i <= kPieces; ++i) {
        const double t = double(i) / kPieces;
        float x, y;
        const bool seen = place(ax + (bx - ax) * t, ay + (by - ay) * t, x, y);
        if (seen && had) vg.drawLine(px, py, x, y, width, paint);
        px = x;
        py = y;
        had = seen;
    }
}

void JPReticle::draw(JVectorCanvas& vg, const JRect& area, float cx, float cy, const Place& place, double pxPerMm,
                     double reachMm, float width, float tick, float gap, const JPaint& paint) const {
    if (kind == Kind::None) return;
    vg.drawLine(area.x, cy, area.x + area.width, cy, width, paint);
    vg.drawLine(cx, area.y, cx, area.y + area.height, width, paint);
    if (!toScale() || !place || pxPerMm <= 0 || reachMm <= 0) return;

    switch (kind) {
        case Kind::Grid: {
            const double step = thinned(spacingMm, pxPerMm, gap);
            const int n = int(std::ceil(reachMm / step));
            for (int i = -n; i <= n; ++i) {
                if (i == 0) continue;   // the cross is there
                line(vg, place, i * step, -reachMm, i * step, reachMm, width, paint);
                line(vg, place, -reachMm, i * step, reachMm, i * step, width, paint);
            }
            break;
        }
        case Kind::Ruler: {
            // Along the machine's axes: a mark at each step, longer at each
            // fifth and longer again at each tenth.
            line(vg, place, -reachMm, 0, reachMm, 0, width, paint);
            line(vg, place, 0, -reachMm, 0, reachMm, width, paint);
            const double step = thinned(spacingMm, pxPerMm, gap);
            const int n = int(std::ceil(reachMm / step));
            for (int i = -n; i <= n; ++i) {
                if (i == 0) continue;
                const double half = (i % 10 == 0 ? 2.0 : i % 5 == 0 ? 1.5 : 1.0) * tick / pxPerMm / 2;
                line(vg, place, i * step, -half, i * step, half, width, paint);
                line(vg, place, -half, i * step, half, i * step, width, paint);
            }
            break;
        }
        case Kind::Circle: {
            const double r = sizeMm / 2;
            float px = 0, py = 0;
            bool had = false;
            for (int i = 0; i <= kCirclePieces; ++i) {
                const double a = 2 * M_PI * i / kCirclePieces;
                float x, y;
                const bool seen = place(r * std::cos(a), r * std::sin(a), x, y);
                if (seen && had) vg.drawLine(px, py, x, y, width, paint);
                px = x;
                py = y;
                had = seen;
            }
            break;
        }
        case Kind::Square: {
            const double h = sizeMm / 2;
            line(vg, place, -h, -h, h, -h, width, paint);
            line(vg, place, h, -h, h, h, width, paint);
            line(vg, place, h, h, -h, h, width, paint);
            line(vg, place, -h, h, -h, -h, width, paint);
            break;
        }
        case Kind::None:
        case Kind::Cross:
            break;
    }
}

} // inline namespace jf
