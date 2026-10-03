// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <string>
#include <vector>

inline namespace jf {

// A graph in a settings form (a calibration's results), as data: what is
// drawn and how, the drawing being the form's (JPPlotView).
//
//  - Lines: each series as a line through its points, x along, y up.
//  - Scatter: each series as dots, the same scale both ways, and a circle
//    of `circle` about the origin when it is more than 0.
//  - Map: `spots` over an area `width` x `height` (y down, as a picture),
//    the area coloured by their values from the least (cool) to the most
//    (hot), each point by the spots nearest it.
struct JPPlot {
    enum class Kind { Lines, Scatter, Map };
    // A series' colour, as a role the form's style gives.
    enum class Tone { First, Second, Muted };
    struct Point { double x = 0, y = 0; };
    struct Series {
        std::string        label;
        Tone               tone = Tone::First;
        std::vector<Point> points;
    };
    struct Spot { double x = 0, y = 0, value = 0; };

    Kind                kind = Kind::Lines;
    std::string         xTitle, yTitle;
    std::vector<Series> series;
    double              circle = 0;
    std::vector<Spot>   spots;
    double              width = 0, height = 0;
};

} // inline namespace jf
