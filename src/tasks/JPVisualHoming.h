// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPVisualTest.h"

#include "camera/JPCameraFeed.h"
#include "machine/JPCell.h"
#include "machine/JPHeadConfig.h"
#include "machine/JPVisionConfig.h"
#include "model/JPConfiguration.h"

#include <optional>

#include <string>

inline namespace jf {

// Finishing a home with the camera: the switches put the head within a
// fraction of a millimetre; the homing mark, seen by a calibrated camera on
// the head, says exactly where. As OpenPnP's (its Fiducial Locator, as the
// FIDUCIAL-HOME part's fiducial vision settings say): the mark found, the
// coordinates corrected so it measures where the head's settings say it is,
// and looked at again, up to Max Vision Passes, until a find corrects them by
// less than Max Linear Offset; the last find's correction stands.
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
    // How the FIDUCIAL-HOME part says the homing fiducial is looked for (its package's fiducial, its vision
    // settings), by the machine's `vision`; none when there is no such part or it cannot be looked for.
    static std::optional<JPVisualTest::Look> homeLook(JPConfiguration& configuration, const JPVisionConfig& vision);
    // The FIDUCIAL-HOME part's package's footprint (drawn while it is looked for); none without one.
    static std::optional<JPFootprint> homeFootprint(JPConfiguration& configuration);
};

} // inline namespace jf
