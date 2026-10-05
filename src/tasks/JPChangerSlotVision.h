// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <opencv2/core.hpp>

#include <string>

inline namespace jf {

// OpenPnP's nozzle tip changer slot vision (ReferenceNozzleTip's
// ensureVisionCalibration and its wizard's template capture): a template is
// the middle of a picture of the slot, Template Width x Height; it is looked
// for in the middle of a later picture, that size and the Tolerance more,
// by normalised cross-correlation.
class JPChangerSlotVision {
public:
    // `mm` in pixels at `mmPerPixel`, rounded up to an even number, as OpenPnP sizes them.
    static int evenPixels(double mm, double mmPerPixel);
    // The middle `width` x `height` of `picture` (within it).
    static cv::Rect middle(const cv::Mat& picture, int width, int height);
    struct Match {
        double score = 0;            // of 1
        double dxPx = 0, dyPx = 0;   // the template's middle from the picture's, pixels (x right, y down)
    };
    // `templ` looked for in the middle `width` x `height` of `picture` (made
    // the template's kind first). False, with why, when it does not fit.
    static bool find(const cv::Mat& picture, const cv::Mat& templ, int width, int height, Match& match, std::string& why);
};

} // inline namespace jf
