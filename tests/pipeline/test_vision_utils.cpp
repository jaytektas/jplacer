// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// OpenPnP's VisionUtilsTest: a 640x480 camera at the origin, 1 mm a pixel: pixel (100, 100) is 220 mm left of
// the middle and 140 mm above it (rows run down, Y up), there on the machine too; 1.776 cm is 17.76 pixels, and
// 13.2 cm² is 1320 square pixels.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "pipeline/JPVisionUtils.h"

using namespace jf;

namespace {

JPVisionUtils::Camera testCamera() {
    JPVisionUtils::Camera c;
    c.location = JPLocation(JPLengthUnit::Millimeters, 0, 0, 0, 0);
    c.unitsPerPixel = JPLocation(JPLengthUnit::Millimeters, 1, 1, 0, 0);
    c.width = 640;
    c.height = 480;
    return c;
}

void testOffsets() {
    const JPVisionUtils::Camera camera = testCamera();
    assert(camera.location == JPLocation(JPLengthUnit::Millimeters, 0, 0, 0, 0));
    assert(camera.width == 640 && camera.height == 480);
    const JPLocation pixelOffsets = JPVisionUtils::pixelCenterOffsets(camera, 100, 100);
    assert(pixelOffsets == JPLocation(JPLengthUnit::Millimeters, -220, 140, 0, 0));
    const JPLocation pixelLocation = JPVisionUtils::pixelLocation(camera, 100, 100);
    assert(pixelLocation == JPLocation(JPLengthUnit::Millimeters, -220, 140, 0, 0));
    // And back.
    const JPVisionUtils::Point p = JPVisionUtils::locationPixelCenterOffsets(camera, pixelLocation);
    assert(p.x == -220 && p.y == -140);
}

void testConversions() {
    const JPVisionUtils::Camera camera = testCamera();
    assert(JPVisionUtils::toPixels(JPLength(1.776, JPLengthUnit::Centimeters), camera) == 17.76);
    assert(JPVisionUtils::toPixels(JPArea(13.2, JPAreaUnit::SquareCentimeters), camera) == 1320.0);
}

} // namespace

int main() {
    testOffsets();
    testConversions();
    return 0;
}
