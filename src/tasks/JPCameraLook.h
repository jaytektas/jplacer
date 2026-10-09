// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "machine/JPSettleTrace.h"

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
    // With `trace`, how it settled is kept there (each picture's difference
    // from the last, by the settle method, against time; with FixedTime too,
    // by the Euclidean difference, for the settling graph). With the
    // camera's Diagnostics, every settle is traced, with its pictures, and
    // handed to the feed's onSettleTrace when no `trace` is asked for.
    static bool settled(JPCameraFeed& feed, JPGrayImage& out, std::string& why, JPSettleTrace* trace = nullptr);
    // The same, the picture in colour as the camera gave it (as a vision pipeline is given it).
    static bool settled(JPCameraFeed& feed, JPFrame& out, std::string& why, JPSettleTrace* trace = nullptr);
    // A picture taken at least `afterMs` after the call.
    static bool taken(JPCameraFeed& feed, JPGrayImage& out, std::string& why, int afterMs);
    // The same, the picture as the camera gave it.
    static bool takenFrame(JPCameraFeed& feed, JPFrame& frame, std::string& why, int afterMs);
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
    static constexpr int kStartPollMs = 10;   // how often a camera just brought on screen is checked

private:
    // settled() without its scripting events.
    static bool settledNow(JPCameraFeed& feed, JPFrame& out, std::string& why, JPSettleTrace* trace);
    // With the camera's Expose each picture?, the exposure set for its brightness and `out` taken again;
    // false only when it could not be (no exposure to set, the camera stopped). Waited for at most kExposeMs.
    static bool exposedNow(JPCameraFeed& feed, JPFrame& out, std::string& why);
    static constexpr int kExposeMs = 3000;
};

} // inline namespace jf
