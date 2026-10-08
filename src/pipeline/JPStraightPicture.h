// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "machine/JPCameraCalibration.h"

#include <opencv2/core.hpp>

#include <memory>

inline namespace jf {

// A camera's pictures straightened for vision pipelines, as OpenPnP gives its pipelines the picture its lens
// calibration corrected: the lens's bending taken out and the machine square to the picture, at one scale both
// ways, centred on the point the camera looks at (JPStraightener, as the camera's view shows it straightened, the
// camera's Crop All Invalid Pixels the same). What the camera does not see is black.
//
// The straightened picture is what a camera with a perfect lens, mounted square, would take: calibration() is that
// camera's, so what a pipeline finds in it is placed on the machine through it as through any calibration (at
// another height too: its scale and lean are the camera's).
class JPStraightPicture {
public:
    // For pictures `calibration` was measured on; one already made for it is shared. Null when the calibration
    // cannot place pixels (not valid, no size).
    static std::shared_ptr<const JPStraightPicture> of(const JPCameraCalibration& calibration, bool lookingUp, double showAll);

    const JPCameraCalibration& calibration() const { return m_cal; }
    // `raw` (as taken) straightened into `out`; false when it is not the calibration's size.
    bool straighten(const cv::Mat& raw, cv::Mat& out) const;

private:
    JPCameraCalibration m_cal;
    cv::Mat             m_mapX, m_mapY;   // each straightened pixel's place in the picture as taken (CV_32F)
};

} // inline namespace jf
