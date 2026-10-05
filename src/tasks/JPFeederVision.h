// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPJobMachine.h"

#include "model/JPFeederTape.h"
#include "pipeline/JPPipelineModel.h"
#include "vision/JPRansac.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

inline namespace jf {

// OpenPnP's FeederVisionHelper.findFeatures: from what a feeder's pipeline
// found in a camera's picture (circles, rotated rectangles or key points), the
// tape's sprocket holes. Those 1.5 mm across (give or take the hole
// tolerance) are fitted with lines of holes 4 mm apart; the best line is the
// nearest the camera at least 3.5 mm off it (looking from a part), or, when
// calibrating from between the holes, the first within the calibration
// tolerance. Its holes, nearest the camera first, give hole 1 and hole 2
// (from a part: the nearest two, hole 1 clockwise of hole 2 as seen from the
// part; calibrating: those nearest the holes set, put back a whole hole pitch
// apart about their middle), the tape's angle, the pick location (from a part:
// the camera's place; calibrating: the old pick location moved with hole 1,
// set onto EIA-481's grid when normalised), and from it the vision offset.
// draw() marks the holes, lines, part numbers and text read on the picture,
// as OpenPnP shows them, crossed out in red when no hole was found.
class JPFeederVision {
public:
    // OpenPnP's FindFeaturesMode; Preview is its null (only showing, nothing required).
    enum class Mode { Preview, FromPickLocationGetHoles, CalibrateHoles, OcrOnly };
    struct Settings {
        JPFeederTape::Params tape;
        bool                 normalizePickLocation = true;
        bool                 snapToAxis = false;
        double               calibrationToleranceMm = 1.95;
        double               sprocketHoleToleranceMm = 0.6;
    };
    // The camera that looked: where from, its scale, a pixel's place on the machine and back.
    using Camera = JPJobMachine::Sight;
    struct Circle {
        double x = 0, y = 0, diameter = 0;
    };
    struct Found {
        std::vector<Circle>         holes;
        std::vector<JPRansac::Line> lines;
        std::optional<JPLocation>   hole1, hole2, pick, visionOffset;
        std::optional<std::string>  ocrText;
    };
    // `results`: the "results" stage's model. False, and why, when the mode needs holes not found.
    static bool find(const JPPipelineModel& results, Mode mode, const Settings& settings, const Camera& camera, Found& found,
                     std::string& why);
    // The found features on `bgr` (the pipeline's working picture, a copy).
    static void draw(cv::Mat& bgr, const Found& found, const Settings& settings, const Camera& camera);
};

} // inline namespace jf
