// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <string>

namespace cv { class Mat; }

inline namespace jf {

// Where a template image (a piece of an earlier picture) is in a picture, as
// OpenPnP's OpenCvVisionProvider.locateTemplateMatches: OpenCV's matchTemplate
// (TM_CCOEFF) over the area of interest, in colour, the best place taken, to
// the pixel. As OpenPnP's, nothing is turned away for matching poorly; its
// score is given for the log.
class JPTemplateFinder {
public:
    // The area to look in, in the picture's pixels (none wide or high: all of it).
    struct Area {
        int x = 0, y = 0, width = 0, height = 0;
        // X and Y from the picture's middle, and X, Y, width and height each
        // kept within 0..512 (OpenPnP's Neoden 4 feeder, for its camera);
        // placed by placed().
        bool fromMiddle = false;
        Area placed(int pictureWidth, int pictureHeight) const;
    };
    struct Result {
        bool        found = false;
        int         x = 0, y = 0;   // the template's top left corner, pixels
        double      score = 0;      // TM_CCOEFF's best
        std::string why;
    };

    // `bgr` and `templ` in colour (BGR).
    static Result find(const cv::Mat& bgr, const cv::Mat& templ, const Area& area);
};

} // inline namespace jf
