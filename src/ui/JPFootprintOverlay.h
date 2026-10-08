// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPCameraView.h"

#include "model/JPFootprint.h"

inline namespace jf {

// A footprint drawn over a camera's picture, as OpenPnP's FootprintReticle:
// each pad's outline (its rounding as OpenPnP rounds it, the inner side
// only when negative) and pin 1's mark, centred where the camera looks, to
// scale, its pads alone (not its body); turned counter-clockwise by what
// `rotationDeg` gives as it is drawn: OpenPnP's CameraView turns every
// reticle by the selected tool's rotation now (a camera moved to a
// placement turned to it, a nozzle as it is turned).
struct JPFootprintOverlay {
    static JPCameraView::Overlay of(const JPFootprint& footprint, std::function<double()> rotationDeg);
};

} // inline namespace jf
