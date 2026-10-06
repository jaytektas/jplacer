// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "model/JPArea.h"
#include "model/JPLength.h"
#include "model/JPLocation.h"

inline namespace jf {

// OpenPnP's VisionUtils: a camera's pixels to places and lengths and back, by its units per pixel. Pixel rows run
// down, the machine's Y up (OpenPnP's right-handed coordinates).
class JPVisionUtils {
public:
    // A camera as these see it: where it looks, its units per pixel (at the height in question), its picture.
    struct Camera {
        JPLocation location { JPLengthUnit::Millimeters };
        JPLocation unitsPerPixel { JPLengthUnit::Millimeters };
        int        width = 0, height = 0;
        // One whose scale is pixels per mm (as a pipeline's).
        static Camera ofScale(double pixelsPerMmX, double pixelsPerMmY, int width = 0, int height = 0);
    };
    struct Point {
        double x = 0, y = 0;
    };

    // Pixel (x, y)'s offset from the picture's middle, in the camera's units.
    static JPLocation pixelCenterOffsets(const Camera& camera, double x, double y);
    // A pixel offset in the camera's units.
    static JPLocation pixelOffsets(const Camera& camera, double offsetX, double offsetY);
    // Where pixel (x, y) is: the camera's location and its offset from the middle.
    static JPLocation pixelLocation(const Camera& camera, double x, double y);
    // The other way: where `location` is seen, in pixels from the picture's middle.
    static Point locationPixelCenterOffsets(const Camera& camera, const JPLocation& location);
    // A length in pixels, at the mean of the two scales (circles are not ovals); an area at both.
    static double toPixels(const JPLength& length, const Camera& camera);
    static double toPixels(const JPArea& area, const Camera& camera);
};

} // inline namespace jf
