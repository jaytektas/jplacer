// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "camera/JPCameraFeed.h"
#include "machine/JPCell.h"
#include "machine/JPHeadConfig.h"
#include "pipeline/JPPipeline.h"

#include <memory>

#include <string>

inline namespace jf {

// Where the homing mark really is, seen by a calibrated head camera: the camera
// is moved to look at the mark's place, the mark is found, and its position
// worked out from the picture. Nothing is reset: this says what visual homing
// would correct, and is the check before letting it.
class JPVisualTest {
public:
    struct Result {
        bool        found = false;
        double      markX = 0, markY = 0;     // where the mark is, machine coordinates
        double      offsetX = 0, offsetY = 0; // mark minus where the head's settings say it is
        double      confidence = 0;
        std::string why;
    };

    // How the mark is looked for, as OpenPnP's visual homing does: the FIDUCIAL-HOME part's size (its
    // package's pad) and its fiducial vision settings' pipeline, prepared (JPFiducialLocator::partLook).
    struct Look {
        double                      diameterMm = 0;
        std::shared_ptr<JPPipeline> pipeline;   // none: jplacer's finder alone
        // Its fiducial vision settings' Max Vision Passes and Max Linear Offset (visual homing's).
        int                         passes = 3;
        double                      maxLinearOffsetMm = 0.2;
    };
    // `look` none: OpenPnP's "Visual homing is missing the FIDUCIAL-HOME part. Please create it."
    static Result run(JPCell& cell, JPCameraFeed& feed, const JPHeadConfig& head, double speed, const Look* look);

};

} // inline namespace jf
