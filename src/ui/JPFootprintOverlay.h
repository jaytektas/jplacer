// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPCameraView.h"

#include "model/JPFootprint.h"

inline namespace jf {

// A footprint drawn over a camera's picture, as OpenPnP's FootprintReticle:
// each pad's outline (its rounding as OpenPnP rounds it, the inner side
// only when negative) and pin 1's mark, centred where the camera looks, to
// scale; turned by `rotationDeg` (counter-clockwise) and, `mirrored`, turned
// over left to right first (a part on a board's underside, seen from above):
// a placement's footprint as it is placed (OpenPnP's PackageReticle), its
// body's outline too.
struct JPFootprintOverlay {
    static JPCameraView::Overlay of(const JPFootprint& footprint, double rotationDeg = 0, bool mirrored = false);
};

} // inline namespace jf
