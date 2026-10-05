// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "model/JPFootprint.h"

#include <opencv2/core.hpp>

#include <map>
#include <mutex>
#include <string>

inline namespace jf {

// OpenPnP's ImageCamera.isPartLocation, for Simulation Mode's Pick & Place
// Checking: whether the picture of the table an image camera shows has a
// part (to pick: its body, dark in the tape) or its pads (to place on:
// OpenPnP's test picture marks them in colour) where the nozzle is.
//
// The footprint, turned as the nozzle is, is drawn as a template at the
// picture's scale and blurred to match tolerantly; the picture around the
// place (five times the template, at least 80 pixels) is cut out and, for
// OpenPnP's test picture, masked to what part bodies or pad marks look like;
// the best match near the middle must score at least the minimum and lie
// within the tolerance (plus a pixel and a half, and 2.5% of the template's
// diagonal, for drawing and rounding).
class JPSimulatedPnpCheck {
public:
    // The image camera's picture: its file, its scale (mm a pixel) and where
    // its bottom left is (the machine's origin less the offset).
    struct Picture {
        std::string path;
        double      unitsPerPixelX = 0, unitsPerPixelY = 0;
        double      offsetX = 0, offsetY = 0;
        // OpenPnP's Filter Test Image Vision: the picture masked as above.
        bool        filterTestImage = true;
    };
    struct Tolerance {
        double distanceMm = 0, minimumScore = 0;
    };

    // Whether the part of `footprint`, at x, y (mm) turned `rotation`
    // degrees, is where the picture shows one: for a pick, its body; for a
    // place, its pads. `detail` says why not (or how near it was).
    bool isPartLocation(const Picture& picture, const JPFootprint& footprint, double x, double y, double rotation,
                        bool pick, const Tolerance& tolerance, std::string& detail);

private:
    // Each picture read once.
    const cv::Mat* picture(const std::string& path, std::string& detail);

    std::mutex                     m_mutex;
    std::map<std::string, cv::Mat> m_pictures;
};

} // inline namespace jf
