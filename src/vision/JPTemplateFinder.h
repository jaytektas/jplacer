// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPGrayImage.h"

#include <string>

inline namespace jf {

// Where a template image (a piece of an earlier picture) is in a picture, as
// OpenPnP's locateTemplateMatches: slid over the area of interest and scored
// by normalised cross-correlation, so the light's brightness and contrast
// do not matter; coarse first (both halved while the template is large),
// then on the full picture near the best, the place found to a fraction of
// a pixel.
class JPTemplateFinder {
public:
    // The area to look in, in the picture's pixels (none wide or high: all of it).
    struct Area {
        int x = 0, y = 0, width = 0, height = 0;
    };
    struct Result {
        bool        found = false;
        double      x = 0, y = 0;   // the template's top left corner, pixels
        double      score = 0;      // of 1
        std::string why;
    };

    static Result find(const JPGrayImage& image, const JPGrayImage& templ, const Area& area, double minScore = 0.5);
};

} // inline namespace jf
