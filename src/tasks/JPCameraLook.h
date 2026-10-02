// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "camera/JPCameraFeed.h"
#include "vision/JPGrayImage.h"

#include <string>

inline namespace jf {

// A picture to measure: one TAKEN after the machine came to rest. Call it once
// a move has finished: it waits for a picture captured at least `settleMs`
// after the call. Going by when a picture was taken, not how many have
// arrived: a camera and its driver hold a few, so the next to arrive can be
// from before the move ended, and the first after it may still be shaking.
class JPCameraLook {
public:
    static bool settled(JPCameraFeed& feed, JPGrayImage& out, std::string& why, int settleMs = kSettleMs);

    static constexpr int kSettleMs  = 150;    // until a camera's own settle setting exists
    static constexpr int kTimeoutMs = 3000;
};

} // inline namespace jf
