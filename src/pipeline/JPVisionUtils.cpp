// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPVisionUtils.h"

#include "model/JPAreaUnits.h"

inline namespace jf {

JPVisionUtils::Camera JPVisionUtils::Camera::ofScale(double pixelsPerMmX, double pixelsPerMmY, int width, int height) {
    Camera c;
    c.unitsPerPixel = JPLocation(JPLengthUnit::Millimeters, 1 / pixelsPerMmX, 1 / pixelsPerMmY, 0, 0);
    c.width = width;
    c.height = height;
    return c;
}

JPLocation JPVisionUtils::pixelCenterOffsets(const Camera& camera, double x, double y) {
    const double imageWidth = camera.width, imageHeight = camera.height;
    return pixelOffsets(camera, x - imageWidth / 2, y - imageHeight / 2);
}

JPLocation JPVisionUtils::pixelOffsets(const Camera& camera, double offsetX, double offsetY) {
    const JPLocation& upp = camera.unitsPerPixel;
    offsetX *= upp.x();
    offsetY *= upp.y();
    return JPLocation(upp.units(), offsetX, -offsetY, 0, 0);
}

JPLocation JPVisionUtils::pixelLocation(const Camera& camera, double x, double y) {
    return camera.location.add(pixelCenterOffsets(camera, x, y));
}

JPVisionUtils::Point JPVisionUtils::locationPixelCenterOffsets(const Camera& camera, const JPLocation& location) {
    const JPLocation& upp = camera.unitsPerPixel;
    const JPLocation l = location.convertToUnits(upp.units())
                             .subtract(camera.location)
                             .multiply(1. / upp.x(), -1. / upp.y(), 0., 0.);
    return { l.x(), l.y() };
}

double JPVisionUtils::toPixels(const JPLength& length, const Camera& camera) {
    const JPLocation& upp = camera.unitsPerPixel;
    const double avgUnitsPerPixel = (upp.x() + upp.y()) / 2;
    return length.convertToUnits(upp.units()).value() / avgUnitsPerPixel;
}

double JPVisionUtils::toPixels(const JPArea& area, const Camera& camera) {
    const JPLocation& upp = camera.unitsPerPixel;
    return area.convertToUnits(JPAreaUnits::fromLengthUnit(upp.units())).value() / (upp.x() * upp.y());
}

} // inline namespace jf
