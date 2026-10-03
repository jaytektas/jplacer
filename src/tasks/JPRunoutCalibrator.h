// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "camera/JPCameraFeed.h"
#include "machine/JPCell.h"
#include "machine/JPRunout.h"

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

    static std::optional<JPRunout> run(JPCell& cell, JPCameraFeed& camera, const JPNozzleConfig& nozzle,
                                       const JPNozzleTipConfig& tip, const Options& options, std::string& why,
                                       const Progress& progress = nullptr);
};

} // inline namespace jf
