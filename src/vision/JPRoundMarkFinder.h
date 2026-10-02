// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPGrayImage.h"
#include "JPRoundMark.h"

inline namespace jf {

// Finds a round mark (a fiducial, the homing mark) of a known size near where
// it should be, with nothing to tune.
//
// CIRCULAR SYMMETRY, not brightness. Around the true centre of a round mark,
// each ring of pixels is even (the mark is the same all the way round) while
// rings at different radii differ (inside the mark, its edge, outside). The
// score at a candidate centre is how much the rings differ from one another
// over how uneven each ring is. It needs no threshold, works the same for a
// bright mark on a dark ground and the reverse, and is barely moved by light
// that changes across the picture. The best centre is then refined to a
// fraction of a pixel, the mark's diameter measured at its edge, and the
// answer accepted only when that diameter agrees with the one expected.
class JPRoundMarkFinder {
public:
    // Whether the mark is brighter than what is around it (copper on solder
    // mask, a white dot), darker (a hole, a via), or either.
    enum class Polarity { Either, Bright, Dark };

    struct Request {
        double expectedX = 0, expectedY = 0;   // where it should be, pixels
        double searchRadius = 0;               // how far from there to look, pixels
        double diameter = 0;                   // its expected diameter, pixels
        double sizeTolerance = 0.25;           // accepted measured/expected - 1, either way
        double minShape      = 0.8;            // accepted share of the edge found round (JPRoundMark::shape)
        Polarity polarity    = Polarity::Either;
    };

    static JPRoundMark find(const JPGrayImage& image, const Request& request);

    // Before a camera is calibrated its scale is not known, so neither is a
    // mark's size in pixels: try sizes from minDiameter to maxDiameter (each
    // a fifth bigger than the last) and keep the best mark found.
    static JPRoundMark findAnySize(const JPGrayImage& image, double expectedX, double expectedY,
                                   double searchRadius, double minDiameter, double maxDiameter,
                                   Polarity polarity = Polarity::Either);

    // The symmetry score at one candidate centre (exposed for diagnostics).
    static double symmetryAt(const JPGrayImage& image, double cx, double cy, double maxRadius);

private:
    // Measure the mark near (x, y), a place the search found at `scale`.
    static JPRoundMark measureAt(const JPGrayImage& image, const Request& rq, double x, double y, double scale);
};

} // inline namespace jf
