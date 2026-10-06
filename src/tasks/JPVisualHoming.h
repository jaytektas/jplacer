// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPVisualTest.h"

#include "camera/JPCameraFeed.h"
#include "machine/JPCell.h"
#include "machine/JPHeadConfig.h"

#include <string>

inline namespace jf {

// Finishing a home with the camera: the switches put the head within a
// fraction of a millimetre; the homing mark, seen by a calibrated camera on
// the head, says exactly where. The coordinates are corrected so the mark
// measures where the head's settings say it is, then it is looked at again
// to check (and corrected again if the first correction left more than a
// trace).
class JPVisualHoming {
public:
    struct Result {
        bool        ok = false;
        double      correctedX = 0, correctedY = 0;   // the total correction made, mm
        double      leftX = 0, leftY = 0;             // what the last look still saw
        std::string why;
    };

    // `look`: how the homing fiducial is looked for (JPVisualTest::Look, the FIDUCIAL-HOME part's).
    static Result run(JPCell& cell, JPCameraFeed& feed, const JPHeadConfig& head, double speed, const JPVisualTest::Look* look);
};

} // inline namespace jf
