// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPJobMachine.h"

#include "model/JPFeeder.h"
#include "pipeline/JPPipeline.h"
#include "vision/JPRansac.h"

#include <optional>
#include <string>
#include <vector>

inline namespace jf {

// OpenPnP's BlindsFeeder.FindFeatures: of the rotated rectangles a blinds
// feeder's pipeline found (those not touching the picture's edge), the
// fiducials (1.4 to 2.3 mm, nearly square, turned 45° in the holder's frame)
// nearest the camera first, and the blinds (pockets: half a pocket pitch
// by the pocket size, give or take, square to the tape) along the tape;
// from a histogram of the blinds' corners across the tape, the pocket size
// and centerline; of their distances along it, the pocket pitch; of their
// places modulo the pitch near the camera, where the cover's blinds are (the
// pocket position). Before the holder's fiducials are set, anything goes; once
// set, only what is near the camera. draw() marks them on the picture.
class JPBlindsVision {
public:
    struct Found {
        std::vector<cv::RotatedRect> blinds, fiducials;
        std::vector<JPRansac::Line>  lines;
        double                       pocketSizeMm = NAN, pocketPositionMm = NAN, pocketPitchMm = NAN, pocketCenterlineMm = NAN;
        std::optional<std::string>   ocrText;
        double                       ocrAvgScore = 0;
    };
    // `pipeline` run on `camera`'s picture; false (and why) when its results are not rotated rectangles.
    static bool find(const JPFeeder& feeder, const JPPipeline& pipeline, const JPJobMachine::Sight& camera, Found& found, std::string& why);
    // The same from the rectangles found (pixels).
    static void find(const JPFeeder& feeder, const std::vector<cv::RotatedRect>& results, const JPJobMachine::Sight& camera, Found& found);
    static void draw(cv::Mat& bgr, const JPFeeder& feeder, const Found& found, const JPJobMachine::Sight& camera);
};

} // inline namespace jf
