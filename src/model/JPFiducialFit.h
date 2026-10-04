// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPAffineTransform.h"
#include "JPLocation.h"

#include <vector>

inline namespace jf {

// The transform that takes where fiducials should be onto where they were
// found, as OpenPnP's Utils2D.deriveAffineTransform: with two, a rotation
// and one scale (the Kabsch fit, the scale from their spacing); with more,
// the least-squares linear map (a reflection taken out), about their
// centroids. In millimetres.
class JPFiducialFit {
public:
    static JPAffineTransform derive(const std::vector<JPLocation>& expected, const std::vector<JPLocation>& measured);
};

} // inline namespace jf
