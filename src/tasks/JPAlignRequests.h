// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPJobMachine.h"

#include "machine/JPVisionConfig.h"
#include "model/JPConfiguration.h"

#include <string>

inline namespace jf {

// How a part is aligned by bottom vision (OpenPnP's getPartAlignment and
// its settings), for a job's placement and for Test Alignment alike.
class JPAlignRequests {
public:
    // A part's alignment: none (false) when bottom vision is off for it or
    // its settings are not enabled. `placeAngle`: the angle it is placed at.
    // `settings`: the bottom vision settings that apply (null: none).
    static bool forPart(const JPConfiguration& config, const JPVisionConfig& vision, const JPPart& part, double partHeightMm,
                        double placeAngle, JPJobMachine::AlignRequest& request,
                        const JPVisionSettings** settings = nullptr);
};

} // inline namespace jf
