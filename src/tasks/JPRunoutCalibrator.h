// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "camera/JPCameraFeed.h"
#include "machine/JPCell.h"
#include "machine/JPRunout.h"
#include "machine/JPScripting.h"
#include "tasks/JPBackgroundCalibration.h"

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
    struct Options {
        double speed = 1.0;   // share of the axes' rates
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
    static std::optional<JPRunout> measure(JPCell& cell, JPCameraFeed& camera, const JPNozzleConfig& nozzle,
                                           const JPNozzleTipConfig& tip, JPScripting* scripting, std::string& words,
                                           const Progress& progress, std::optional<JPBackgroundCalibration::Result>& background);
};

} // inline namespace jf
