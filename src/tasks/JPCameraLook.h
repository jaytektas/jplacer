// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "camera/JPCameraFeed.h"
#include "vision/JPGrayImage.h"

#include <string>

inline namespace jf {

// A picture to measure: one taken after the machine came to rest. Waits for
// `settleFrames` new frames after the call (a camera's frames lag the
// machine by a frame or two, and the first after a move may still be blurred).
class JPCameraLook {
public:
    static bool settled(JPCameraFeed& feed, JPGrayImage& out, std::string& why, int settleFrames = kSettleFrames);

    static constexpr int kSettleFrames = 3;   // until a camera's own settle settings exist
    static constexpr int kTimeoutMs    = 3000;
};

} // inline namespace jf
