// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "camera/JPCameraFeed.h"
#include "machine/JPCell.h"
#include "vision/JPGrayImage.h"
#include "vision/JPRoundMarkFinder.h"

#include <string>

inline namespace jf {

// A picture to measure: one TAKEN after the machine came to rest. Call it once
// a move has finished. Going by when a picture was taken, not how many have
// arrived: a camera and its driver hold a few, so the next to arrive can be
// from before the move ended, and the first after it may still be shaking.
class JPCameraLook {
public:
    // A picture taken once the camera settled, as the camera's settling says
    // (JPCameraConfig::Settle). Watching for motion, a timeout still gives
    // the last picture (as OpenPnP does), and the log says so.
    static bool settled(JPCameraFeed& feed, JPGrayImage& out, std::string& why);
    // A picture taken at least `afterMs` after the call.
    static bool taken(JPCameraFeed& feed, JPGrayImage& out, std::string& why, int afterMs);
    // How much `b` differs from `a` by `method` (Maximum, Mean, Euclidean,
    // Square), as a percentage of full scale, in a centred circle of
    // `maskCircle` of the smaller side (0: everywhere).
    static double difference(const JPGrayImage& a, const JPGrayImage& b, const std::string& method, double maskCircle);
    // The camera's calibration for the pictures it is taking (waiting for
    // one, to know their size). False (and why) when it has none at that size.
    static bool calibration(JPCell& cell, JPCameraFeed& feed, JPCameraCalibration& out, std::string& why);
    // A settled picture with the room's light taken out: one with the
    // camera's light off and one with it on, the first taken from the
    // second. What is left is what the camera's light lights, whatever the
    // sun or the room is doing. The light is left on. False (and why) when
    // the camera has no light to switch.
    static bool lightOnly(JPCell& cell, JPCameraFeed& feed, JPGrayImage& out, std::string& why);
    // Find a round mark in `picture` (a settled one); not found there, look
    // again with the room's light taken out (lightOnly), where the camera has
    // a light: trying harder before failing.
    static JPRoundMark findTryingHarder(JPCell& cell, JPCameraFeed& feed, const JPGrayImage& picture,
                                        const JPRoundMarkFinder::Request& request);

    static constexpr int kTimeoutMs = 3000;   // for a picture to arrive at all
};

} // inline namespace jf
