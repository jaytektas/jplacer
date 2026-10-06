// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "pipeline/JPPipeline.h"
#include "vision/JPGrayImage.h"
#include "vision/JPRoundMark.h"

#include <string>

inline namespace jf {

// A round mark found by a vision pipeline, as OpenPnP's camera calibration
// finds its fiducial (CalibrateCameraProcess) and its nozzle tip calibration
// finds the tip (ReferenceNozzleTipCalibration.findCircle): the picture given
// to its ImageCapture, the expected centre, distance and diameter set as the
// properties of `control` ("DetectCircularSymmetry", "nozzleTip"), and the
// mark the "results" stage's (a keypoint, circle or rotated rectangle; more
// than one, as OpenPnP: none taken), whose centre is then measured to a
// fraction of a pixel close by. The pipeline is the camera's or the tip's
// own (editable; OpenPnP's default unless changed).
class JPPipelineMarkFinder {
public:
    explicit JPPipelineMarkFinder(const std::string& pipelineXml, std::string control = "DetectCircularSymmetry");
    // A pipeline already prepared (a fiducial's: its part's vision settings, properties set), with the
    // camera's scale (pixels a millimetre) for what is set in millimetres.
    JPPipelineMarkFinder(JPPipeline prepared, std::string control, double pxPerMmX, double pxPerMmY);

    // Near (x, y), within maxDistance pixels, about `diameter` pixels across; its centre refined where at least
    // `minShape` of its edge is round (0: JPRoundMarkFinder's own share).
    JPRoundMark find(const JPGrayImage& image, double x, double y, double maxDistance, double diameter, double minShape = 0);
    // Its size not known yet (before calibration): sizes from minDiameter to
    // maxDiameter, each a fifth bigger, the most symmetric mark kept.
    JPRoundMark findAnySize(const JPGrayImage& image, double x, double y, double maxDistance, double minDiameter,
                            double maxDiameter);

private:
    void       useCapture();
    JPPipeline  m_pipeline;
    std::string m_control;
    bool       m_parsed = false;
    cv::Mat    m_picture;   // what its ImageCapture gives
};

} // inline namespace jf
