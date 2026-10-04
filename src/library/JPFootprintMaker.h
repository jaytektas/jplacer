// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPFootprint.h"

#include <string>

inline namespace jf {

// Footprints made from a package's numbers, at IPC-7351's zero orientation
// (pin 1 upper left; a two-terminal part lying along X, pin 1 on the left).
// Millimetres, Y up, the footprint's centre at 0, 0; pads are named "1".."n"
// (an exposed pad n + 1), and pin 1 is pad "1".
class JPFootprintMaker {
public:
    // Two rows of pads, `span` apart centre to centre along X: pins 1..n/2
    // down the left from the top, the rest up the right (SOIC, TSSOP, and a
    // two-terminal chip with pins = 2).
    struct Dual {
        int    pins = 8;
        double pitch = 1.27;
        double span = 4.95;              // pad centre to pad centre, across
        double padLength = 1.95;         // along X (out from the body)
        double padWidth = 0.6;           // along Y
        double bodyWidth = 3.9, bodyLength = 4.9;
    };
    static JPFootprint dual(const std::string& name, const Dual& d);

    // Pads on four sides, counted anticlockwise from pin 1 at the top of the
    // left side (QFN, QFP), with an optional exposed pad in the middle.
    struct Quad {
        int    pinsPerSide = 8;
        double pitch = 0.5;
        double span = 6.0;               // pad centre to pad centre, across
        double padLength = 0.85, padWidth = 0.25;
        double bodySize = 5.0;
        double exposedPad = 0;           // its side; 0: none
    };
    static JPFootprint quad(const std::string& name, const Quad& q);
};

} // inline namespace jf
