// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "machine/JPCameraCalibration.h"
#include "ui/JPCameraPanel.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>

inline namespace jf {

// OpenPnP's Estimate Z Coordinate of Object (its EstimateObjectZCoordinateProcess),
// on a camera calibrated at two heights, in the camera's instructions panel:
// the camera (or, under a fixed camera, the nozzle holding the object) is
// jogged so a sharp feature is in view, and it is clicked; jogged again, it
// is clicked where it is now; its Z is said (JPCameraCalibration::estimateObjectZ),
// with Again to measure another, or Cancel.
class JPlacerEstimateZ {
public:
    // Where the camera is looking (for a fixed one, where the nozzle is): X and Y, or none when not known.
    using Where = std::function<std::optional<std::pair<double, double>>()>;
    // The camera's calibration for pictures width x height.
    using Calibration = std::function<JPCameraCalibration(int width, int height)>;

    ~JPlacerEstimateZ();
    // Begun on `panel` (a process already under way on another is ended first).
    void start(JPCameraPanel& panel, bool fixed, Where where, Calibration calibration);
    void cancel();

private:
    void step(int s);
    void clicked(double px, double py);

    JPCameraPanel* m_panel = nullptr;
    bool           m_fixed = false;
    Where          m_where;
    Calibration    m_calibration;
    int            m_step = -1;
    double         m_px1 = 0, m_py1 = 0;
    std::pair<double, double> m_at1 {};
    std::string    m_estimate = "unavailable";
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

} // inline namespace jf
