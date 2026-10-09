// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "pipeline/JPPipeline.h"

#include <opencv2/core.hpp>
#include "camera/JPFrame.h"
#include "machine/JPCameraCalibration.h"
#include "machine/JPCameraConfig.h"
#include "vision/JPRoundMark.h"

#include <string>

inline namespace jf {

// A round mark found by a vision pipeline, as OpenPnP's camera calibration
// finds its fiducial (CalibrateCameraProcess) and its nozzle tip calibration
// finds the tip (ReferenceNozzleTipCalibration.findCircle): the picture given
// to its ImageCapture, the expected centre, distance and diameter set as the
// properties of `control` ("DetectCircularSymmetry", "nozzleTip"), and the
// mark the "results" stage's (a keypoint, circle or rotated rectangle; more
// than one, as OpenPnP: none taken), on the picture in colour as the camera
// gave it. The pipeline is the camera's or the tip's own (editable; OpenPnP's
// default unless changed).
class JPPipelineMarkFinder {
public:
    explicit JPPipelineMarkFinder(const std::string& pipelineXml, std::string control = "DetectCircularSymmetry");
    // A pipeline already prepared (a fiducial's: its part's vision settings, properties set), with the
    // camera's scale (pixels a millimetre) for what is set in millimetres.
    JPPipelineMarkFinder(JPPipeline prepared, std::string control, double pxPerMmX, double pxPerMmY);

    // Near (x, y), within maxDistance pixels, about `diameter` pixels across.
    JPRoundMark find(const JPFrame& image, double x, double y, double maxDistance, double diameter);
    // The same in a picture already in BGR (a straightened one, as a pipeline is given it).
    JPRoundMark find(const cv::Mat& bgr, double x, double y, double maxDistance, double diameter);
    // Its size not known yet (before calibration): sizes from minDiameter to
    // maxDiameter, each a fifth bigger, the most symmetric mark kept.
    JPRoundMark findAnySize(const JPFrame& image, double x, double y, double maxDistance, double minDiameter,
                            double maxDiameter);
    JPRoundMark findAnySize(const cv::Mat& bgr, double x, double y, double maxDistance, double minDiameter,
                            double maxDiameter);

    // A round mark near (x, y) on the machine, the camera looking from (viewX, viewY), in `frame` as the camera
    // took it: found by `cam`'s calibration pipeline (OpenPnP's Advanced Calibration's; its own when edited) on the
    // picture in colour, straightened by `calibration` (as OpenPnP's pipelines are given pictures), within
    // `searchMm`, `diameterMm` across (0: at any size from `leastMm` to `mostMm`, its size then into it). Where it
    // is on the machine into mx, my; not found, why (in the mark).
    static JPRoundMark onMachine(const JPCameraConfig& cam, const JPCameraCalibration& calibration, const JPFrame& frame,
                                 double x, double y, double viewX, double viewY, double searchMm, double& diameterMm,
                                 double leastMm, double mostMm, double& mx, double& my);

private:
    void       useCapture();
    JPPipeline  m_pipeline;
    std::string m_control;
    bool       m_parsed = false;
    cv::Mat    m_picture;   // what its ImageCapture gives
};

} // inline namespace jf
