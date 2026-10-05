// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPJobMachine.h"

#include <optional>
#include <string>

inline namespace jf {

// OpenPnP's AbstractNozzle.prepareForPickAndPlaceArticulation: the rotation
// mode offset a nozzle takes for a part picked at `pickAngle` to be placed
// at `placeAngle`, by its Rotation Mode (JPJobMachine::Nozzle::rotationMode):
// AbsolutePartAngle none; PlacementAngle the placement's angle;
// MinimalRotation the pick's angle from where its axis is; LimitedArticulation
// so the turn from pick to place, with its pick and alignment articulation to
// spare, sits about the middle of its axis's range.
class JPRotationMode {
public:
    // `axisNow`: where its rotation axis is (none: not known).
    static std::optional<double> offset(const JPJobMachine::Nozzle& nozzle, std::optional<double> axisNow, double pickAngle,
                                        double placeAngle);
    // That offset worked out for `nozzleId` and given it on `machine`.
    static std::optional<double> prepare(JPJobMachine& machine, const std::string& nozzleId, double pickAngle, double placeAngle);
};

} // inline namespace jf
