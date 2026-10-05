// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPFrame.h"

inline namespace jf {

// OpenPnP's camera image transforms that still apply under its advanced
// calibration (jplacer's calibration being of that kind): de-interlacing and
// cropping, done to each picture as it comes from the camera, before its
// white balance and everything else.
class JPImageTransform {
public:
    // A picture of two fields stacked (the even lines' half over the odd
    // lines') woven back into one.
    static void deinterlace(JPFrame& frame);
    // Cut to `width` x `height` about its middle; 0 (or more than it has) keeps that side.
    static void crop(JPFrame& frame, int width, int height);
};

} // inline namespace jf
