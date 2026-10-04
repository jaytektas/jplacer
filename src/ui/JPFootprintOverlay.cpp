// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPFootprintOverlay.h"

#include "model/JPLength.h"

#include <j/core/JStyle.h>

#include <algorithm>
#include <cmath>
#include <vector>

inline namespace jf {

namespace {

// Points along a quarter circle, for a rounded corner.
constexpr int kArcSteps = 6;
// Pin 1's mark: this share of the pad's smaller side across, at most one of
// the footprint's units (OpenPnP's Pad.getShape).
constexpr double kMarkShare = 0.9, kMarkMost = 1.0;
constexpr int kMarkSteps = 16;

struct P {
    double x, y;
};

// A pad's outline about its centre before it is turned: a rectangle whose
// corners are rounded by `r` (all four, or with `innerOnly` those on its -X
// half, as OpenPnP adds a square right half to a rounded rectangle).
std::vector<P> outline(double w, double h, double r, bool innerOnly) {
    std::vector<P> pts;
    const double hw = w / 2, hh = h / 2;
    auto corner = [&](double cx, double cy, double a0, bool rounded, double sx, double sy) {
        if (!rounded || r <= 0) {
            pts.push_back({ sx, sy });
            return;
        }
        for (int i = 0; i <= kArcSteps; ++i) {
            const double a = a0 + (M_PI / 2) * double(i) / kArcSteps;
            pts.push_back({ cx + r * std::cos(a), cy + r * std::sin(a) });
        }
    };
    corner(hw - r, -hh + r, -M_PI / 2, !innerOnly, hw, -hh);   // bottom right
    corner(hw - r, hh - r, 0, !innerOnly, hw, hh);             // top right
    corner(-hw + r, hh - r, M_PI / 2, true, -hw, hh);          // top left
    corner(-hw + r, -hh + r, M_PI, true, -hw, -hh);            // bottom left
    return pts;
}

} // namespace

JPCameraView::Overlay JPFootprintOverlay::of(const JPFootprint& footprint) {
    return [f = footprint](JVectorCanvas& vg, const JPReticle::Place& place, float line) {
        const double mm = JPLength(1, f.units).convertToUnits(JPLengthUnit::Millimeters).value();
        const uint8_t* c = Colors::Warning;
        const JPaint paint = JPaint::solid(rgb(c[0], c[1], c[2]));
        auto draw = [&](const std::vector<P>& pts, const JPFootprint::Pad& pad) {
            const double a = pad.rotation * M_PI / 180, ca = std::cos(a), sa = std::sin(a);
            std::vector<JVectorCanvas::JVec2> screen;
            for (const P& p : pts) {
                const double x = (pad.x + p.x * ca - p.y * sa) * mm, y = (pad.y + p.x * sa + p.y * ca) * mm;
                float sx, sy;
                if (!place(x, y, sx, sy)) return;
                screen.push_back({ sx, sy });
            }
            vg.strokePolyline(screen, line, paint, true);
        };
        for (const JPFootprint::Pad& pad : f.pads) {
            // OpenPnP's rounding: a corner arc as wide as the smaller side times the roundness.
            const double arc = std::min(pad.width, pad.height) * pad.roundness / 100;
            draw(outline(pad.width, pad.height, std::fabs(arc) / 2, arc < 0), pad);
            if (pad.mark) {
                const double r = std::min(std::min(pad.width, pad.height) * kMarkShare, kMarkMost) / 2;
                std::vector<P> ring;
                for (int i = 0; i < kMarkSteps; ++i) {
                    const double t = 2 * M_PI * i / kMarkSteps;
                    ring.push_back({ r * std::cos(t), r * std::sin(t) });
                }
                draw(ring, pad);
            }
        }
    };
}

} // inline namespace jf
