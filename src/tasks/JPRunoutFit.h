// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "machine/JPRunout.h"

#include <optional>
#include <string>
#include <vector>

inline namespace jf {

// A runout (JPRunout) from the tip's measured offsets, as OpenPnP's ReferenceNozzleTipCalibration makes it for
// its compensation algorithm:
//   Model, ModelNoOffset, ModelCameraOffset: a circle fitted by the Kasa method (its centre and radius), the
//     phase the average of each measurement's angle less the angle it was found at about the centre;
//   their Affine ones: the affine transform taking where a tip of 1 mm runout would be (at each angle) onto where
//     it was found (OpenPnP's deriveAffineTransform): its translation the centre, the geometric mean of its scales
//     the radius (none when noise makes one negative), its rotation the phase (negated);
//   Table: the measurements as they are.
// Nothing for fewer than three measurements, or an algorithm OpenPnP does not have.
class JPRunoutFit {
public:
    static std::optional<JPRunout> fit(const std::vector<JPRunout::Point>& points, const std::string& algorithm);
};

} // inline namespace jf
