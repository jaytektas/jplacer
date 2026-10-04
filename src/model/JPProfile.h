// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPLengthUnit.h"

#include "openpnp/JPXmlElement.h"
#include "openpnp/JPXmlNode.h"

#include <optional>
#include <vector>

inline namespace jf {

// A board or panel's outline, as OpenPnP's GeometricPath2D (a Java Path2D):
// segment types (0 move, 1 line, 2 quad, 3 cubic, 4 close) and their
// points, in units.
class JPProfile {
public:
    enum Segment { MoveTo = 0, LineTo = 1, QuadTo = 2, CubicTo = 3, Close = 4 };

    JPLengthUnit        units = JPLengthUnit::Millimeters;
    std::optional<int>  windingRule;
    std::vector<int>    segmentTypes;
    std::vector<double> segmentPoints;

    // A rectangle from the origin, as OpenPnP makes when there is none.
    static JPProfile rectangle(double width, double height, JPLengthUnit units);

    static JPProfile fromXml(const JPXmlElement& e);
    JPXmlNode toXml(const char* name = "profile") const;
};

} // inline namespace jf
