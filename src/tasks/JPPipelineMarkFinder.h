// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "pipeline/JPPipeline.h"
#include "vision/JPGrayImage.h"
#include "vision/JPRoundMark.h"

#include <string>

inline namespace jf {

// A round mark found by a vision pipeline, as OpenPnP's camera calibration
// finds its fiducial (CalibrateCameraProcess): the picture given to its
// ImageCapture, the expected centre, distance and diameter set as its
// "DetectCircularSymmetry" properties, and the mark its "results" keypoint,
// whose centre is then measured to a fraction of a pixel close by.
// The pipeline is the camera's own (editable; OpenPnP's default unless
// changed): JPDefaultPipelines::cameraCalibration.
class JPPipelineMarkFinder {
public:
    explicit JPPipelineMarkFinder(const std::string& pipelineXml);

    // Near (x, y), within maxDistance pixels, about `diameter` pixels across.
    JPRoundMark find(const JPGrayImage& image, double x, double y, double maxDistance, double diameter);
    // Its size not known yet (before calibration): sizes from minDiameter to
    // maxDiameter, each a fifth bigger, the most symmetric mark kept.
    JPRoundMark findAnySize(const JPGrayImage& image, double x, double y, double maxDistance, double minDiameter,
                            double maxDiameter);

private:
    JPPipeline m_pipeline;
    bool       m_parsed = false;
    cv::Mat    m_picture;   // what its ImageCapture gives
};

} // inline namespace jf
