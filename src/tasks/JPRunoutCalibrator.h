// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "camera/JPCameraFeed.h"
#include "machine/JPCell.h"
#include "machine/JPRunout.h"
#include "machine/JPScripting.h"
#include "tasks/JPBackgroundCalibration.h"
#include "vision/JPGrayImage.h"

#include <opencv2/core.hpp>

#include <functional>
#include <optional>
#include <string>

inline namespace jf {

// Measures a nozzle tip's runout on a nozzle with a fixed camera looking up
// (JPRunout): the nozzle over the camera, down to its focus (plus the tip's
// Z offset), turned to each of `divisions` angles round the circle; at each
// the tip's centre is found and its offset from where the nozzle was sent
// kept; the circle fitted through them. Compensation plays no part (the
// axes are moved directly). Up to `misdetects` angles may fail. The nozzle
// goes back up to safe Z, whatever happens.
//
// Runs on a thread of its own: it waits on moves and pictures.
class JPRunoutCalibrator {
public:
    // Each find (on the calibration's thread): the picture, where the tip's end was found in it and how big
    // (pixels), and the step ("turned to 60 deg (3 of 6)"), for it to be shown where it was found.
    using Found = std::function<void(const cv::Mat& picture, double x, double y, double diameterPx, const std::string& step)>;
    struct Options {
        double speed = 1.0;   // share of the axes' rates
        Found  found;
    };
    using Progress = std::function<void(const std::string&)>;

    // With `background`, each picture the tip is found in is given to it
    // (OpenPnP's background calibration rides on runout calibration).
    static std::optional<JPRunout> run(JPCell& cell, JPCameraFeed& camera, const JPNozzleConfig& nozzle,
                                       const JPNozzleTipConfig& tip, const Options& options, std::string& why,
                                       const Progress& progress = nullptr, JPBackgroundCalibration* background = nullptr);
    // A measuring as a task makes it, on the calling thread (a job's): NozzleCalibration's scripting events
    // round it (with `scripting`), the background calibrated along with it when the tip asks for it; `words`
    // says what was found, or why not.
    // OpenPnP's Calibrate Camera Position and Rotation: the tip, its runout measured and compensated, sent round a
    // circle about the camera looking up (the tip's Excenter Ratio of the picture's smaller side out), found at
    // each of its Circle Divisions angles; where the camera is (its middle on the machine) and how far its
    // picture is turned, from where the tip was sent against where it was seen: by the affine transform taking
    // the one onto the other for an Affine algorithm, else by the circle through what was seen (its centre the
    // camera's error, its phase the turn).
    struct CameraFix {
        double x = 0, y = 0;     // where the camera's middle is (mm)
        double turnDeg = 0;      // how far what it sees is turned from the machine (its picture to be turned back)
        double rmsMm = 0;        // how well it fits
        int    points = 0;
    };
    static std::optional<CameraFix> calibrateCamera(JPCell& cell, JPCameraFeed& camera, const JPNozzleConfig& nozzle,
                                                    const JPNozzleTipConfig& tip, const Options& options, std::string& why,
                                                    const Progress& progress = nullptr);
    static std::optional<JPRunout> measure(JPCell& cell, JPCameraFeed& camera, const JPNozzleConfig& nozzle,
                                           const JPNozzleTipConfig& tip, JPScripting* scripting, std::string& words,
                                           const Progress& progress, std::optional<JPBackgroundCalibration::Result>& background,
                                           const Found& found = nullptr);
};

} // inline namespace jf
