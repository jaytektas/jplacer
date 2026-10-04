// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPGrayImage.h"

#include <functional>
#include <string>
#include <vector>

inline namespace jf {

// Where a part is in a picture, and how it is turned: its pads (or its
// body), drawn as they should look at each angle tried through what a pixel
// is on the machine, slid over the picture and scored by normalised
// cross-correlation, so its brightness and contrast do not matter; coarse
// first (the pictures halved), then on the full picture, the place and
// angle found to a fraction. Its size and shape come with the part (its
// package's footprint), so there is nothing to tune.
class JPPartFinder {
public:
    // A pad (or the body) in the part's own millimetres: its centre, size and turn.
    struct Rect {
        double x = 0, y = 0, width = 0, height = 0, rotation = 0;
    };
    struct Request {
        double expectedX = 0, expectedY = 0;   // where the part's centre should be, pixels
        double angle = 0;                      // how it should be turned, degrees (the machine's)
        double angleRange = 10;                // how far either way to look, degrees (180: all round)
        double searchMm = 2;                   // how far from there to look
        double minScore = 0.4;                 // a match scoring less (of 1) is not one
        // Where on the machine a pixel is (mm), for the camera as it looked.
        std::function<bool(double px, double py, double& mx, double& my)> toMachine;
    };
    struct Result {
        bool        found = false;
        double      x = 0, y = 0;   // where its centre is, pixels
        double      angle = 0;      // how it is turned, degrees
        double      score = 0;
        std::string why;
    };

    static Result find(const JPGrayImage& image, const std::vector<Rect>& shape, const Request& request);
};

} // inline namespace jf
