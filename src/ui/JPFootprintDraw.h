// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "library/JPFootprint.h"

#include <j/graphics/VectorGraphics.h>

#include <functional>

inline namespace jf {

// A footprint drawn: its body outline, each pad filled (turned as it is),
// and a dot on pin 1, through `place`, which says where on screen a point
// of the footprint (its millimetres) is. One drawing serves a picture of the
// footprint alone and the footprint over the camera's picture of a board.
class JPFootprintDraw {
public:
    using Place = std::function<bool(double xMm, double yMm, float& x, float& y)>;
    struct Colours {
        JColor pad, pin1, body;
    };

    static void draw(JVectorCanvas& vg, const JPFootprint& f, const Place& place, float line, const Colours& colours);
};

} // inline namespace jf
