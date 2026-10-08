// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPFootprintOverlay.h"

#include "model/JPLength.h"

#include <j/core/JStyle.h>

#include <cmath>
#include <vector>

inline namespace jf {

JPCameraView::Overlay JPFootprintOverlay::of(const JPFootprint& footprint, std::function<double()> rotationDeg) {
    return [f = footprint, rotationDeg = std::move(rotationDeg)](JVectorCanvas& vg, const JPReticle::Place& place, float line) {
        const double a = (rotationDeg ? rotationDeg() : 0.0) * M_PI / 180.0, ca = std::cos(a), sa = std::sin(a);
        const double mm = JPLength(1, f.units).convertToUnits(JPLengthUnit::Millimeters).value();
        const uint8_t* c = Colors::Warning;
        const JPaint paint = JPaint::solid(rgb(c[0], c[1], c[2]));
        for (const JPFootprint::Outline& o : f.padsOutlines()) {
            std::vector<JVectorCanvas::JVec2> screen;
            for (const JPFootprint::Point& p : o) {
                const double x = p.x * mm, y = p.y * mm;
                float sx, sy;
                if (!place(x * ca - y * sa, x * sa + y * ca, sx, sy)) break;
                screen.push_back({ sx, sy });
            }
            if (screen.size() == o.size()) vg.strokePolyline(screen, line, paint, true);
        }
    };
}

} // inline namespace jf
