// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPFootprintOverlay.h"

#include "model/JPLength.h"

#include <j/core/JStyle.h>

#include <vector>

inline namespace jf {

JPCameraView::Overlay JPFootprintOverlay::of(const JPFootprint& footprint) {
    return [f = footprint](JVectorCanvas& vg, const JPReticle::Place& place, float line) {
        const double mm = JPLength(1, f.units).convertToUnits(JPLengthUnit::Millimeters).value();
        const uint8_t* c = Colors::Warning;
        const JPaint paint = JPaint::solid(rgb(c[0], c[1], c[2]));
        for (const JPFootprint::Outline& o : f.padsOutlines()) {
            std::vector<JVectorCanvas::JVec2> screen;
            for (const JPFootprint::Point& p : o) {
                float sx, sy;
                if (!place(p.x * mm, p.y * mm, sx, sy)) break;
                screen.push_back({ sx, sy });
            }
            if (screen.size() == o.size()) vg.strokePolyline(screen, line, paint, true);
        }
    };
}

} // inline namespace jf
