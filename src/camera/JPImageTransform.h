// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPFrame.h"

inline namespace jf {

// OpenPnP's camera image transforms, done to each picture as it comes from
// the camera, before everything else (so the camera is calibrated for the
// picture they give): de-interlacing and cropping, and OpenPnP's Scale,
// Rotation, Offset and Flip, in that order.
class JPImageTransform {
public:
    // A picture of two fields stacked (the even lines' half over the odd
    // lines') woven back into one.
    static void deinterlace(JPFrame& frame);
    // Cut to `width` x `height` about its middle; 0 (or more than it has) keeps that side.
    static void crop(JPFrame& frame, int width, int height);
    // Resized to `width` x `height` (0: that side as it is).
    static void scale(JPFrame& frame, int width, int height);
    // Turned `degrees` counter-clockwise about its middle, onto a picture as big as the turned one's bounds.
    static void rotate(JPFrame& frame, double degrees);
    // Moved by (dx, dy) pixels, the rest black.
    static void offset(JPFrame& frame, int dx, int dy);
    // OpenPnP's flips: `flipX` about the X axis (upside down), `flipY` about the Y axis (left to right).
    static void flip(JPFrame& frame, bool flipX, bool flipY);
};

} // inline namespace jf
