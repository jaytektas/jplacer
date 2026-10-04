// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPGrayImage.h"

#include <string>

inline namespace jf {

// Where a pattern is in a picture: a part's pads drawn as they should look
// (bright copper on darker mask), or a patch of an earlier picture (its
// learned look). The pattern is slid over the picture within a search radius
// of where it is expected and scored by normalised cross-correlation, so the
// picture's brightness and contrast do not matter; coarse first (a quarter
// of the size each way), then on the full picture, the best place found to
// a fraction of a pixel by a parabola through its neighbours' scores. Its
// size and shape come with the pattern, so there is nothing to tune.
class JPPatternFinder {
public:
    struct Request {
        double expectedX = 0, expectedY = 0;   // where the pattern's centre should be, pixels
        double searchRadius = 0;               // how far from there to look, pixels
        double minScore = 0.6;                 // a match scoring less (of 1) is not one
    };
    struct Result {
        bool        found = false;
        double      x = 0, y = 0;              // where the pattern's centre is, pixels
        double      score = 0;
        std::string why;
    };

    // `pattern`'s centre is at (`centreX`, `centreY`) in its own pixels.
    static Result find(const JPGrayImage& image, const JPGrayImage& pattern, double centreX, double centreY,
                       const Request& request);
};

} // inline namespace jf
