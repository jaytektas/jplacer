// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "camera/JPCameraFeed.h"
#include "machine/JPCell.h"

#include <functional>
#include <optional>
#include <string>

inline namespace jf {

// OpenPnP's AutoFocusProvider: a tool held over a fixed camera brought into
// focus between two heights. Each pass steps through the range (at most 11
// steps, no finer than the focal resolution), scores each picture's
// sharpness (focusScore) and narrows the range to the steps either side of
// the best, coming in from a millimetre beyond its start each time (backlash
// in Z), until a step is within 1.5 focal resolutions: the best step is the
// focus. Runs on a thread of its own.
class JPAutoFocus {
public:
    struct Request {
        JPMountConfig tool;              // what is moved (a nozzle), over the camera
        double        x = 0, y = 0;      // where (the camera's centre), the tool's X and Y
        std::optional<double> rotation;  // kept as it is when none
        double        z0 = 0, z1 = 0;    // the range: from z0 (above, the part's top) to z1 (the camera's focus)
        double        subjectMaxSizeMm = 0;   // how much of the picture is looked at (a circle this wide)
        double        mmPerPixel = 0;    // the camera's scale, as calibrated
        JPCameraConfig::AutoFocus settings;
        double        machineSpeed = 1;
    };
    // `show(frame, text)`: what it saw at each step (Show Diagnostics). The
    // tool's Z in focus; none (and why) when it could not be found.
    static std::optional<double> run(JPCell& cell, JPCameraFeed& feed, const Request& rq,
                                     const std::function<void(const JPFrame&, const std::string&)>& show, std::string& why);

    // OpenPnP's focusScore: within a circle `diameter` pixels across in the
    // middle of an RGBA picture, each pixel's edge (its difference from its
    // right and lower neighbours, over the colours); the score is the edge
    // reached by the hardest diameter^1.3 / 10 pixels (a fractile). With
    // `marked`, the circle's crop (diameter + 1 square) with those pixels in
    // green and the circle's rim in black.
    static double focusScore(const JPFrame& picture, int diameter, JPFrame* marked = nullptr);
};

} // inline namespace jf
