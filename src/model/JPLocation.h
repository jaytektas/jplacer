// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPLength.h"

#include <optional>
#include <string>

inline namespace jf {

// A place and a rotation, in units, as OpenPnP's Location: X, Y, Z and a
// rotation in degrees. Arithmetic converts the other operand to this one's
// units; most of it leaves the rotation as this one's.
class JPLocation {
public:
    JPLocation() = default;
    explicit JPLocation(JPLengthUnit units) : m_units(units) {}
    JPLocation(JPLengthUnit units, double x, double y, double z, double rotation)
        : m_units(units), m_x(x), m_y(y), m_z(z), m_rotation(rotation) {}

    static JPLocation origin() { return JPLocation(JPLengthUnit::Millimeters); }

    JPLengthUnit units() const { return m_units; }
    double x() const { return m_x; }
    double y() const { return m_y; }
    double z() const { return m_z; }
    double rotation() const { return m_rotation; }
    JPLength lengthX() const { return JPLength(m_x, m_units); }
    JPLength lengthY() const { return JPLength(m_y, m_units); }
    JPLength lengthZ() const { return JPLength(m_z, m_units); }

    JPLocation convertToUnits(JPLengthUnit units) const;
    double linearDistanceTo(const JPLocation& l) const;
    double linearDistanceTo(double x, double y) const;
    double xyzDistanceTo(const JPLocation& l) const;
    JPLength linearLengthTo(const JPLocation& l) const { return JPLength(linearDistanceTo(l), m_units); }

    JPLocation subtract(const JPLocation& l) const;
    JPLocation subtractWithRotation(const JPLocation& l) const;
    JPLocation add(const JPLocation& l) const;
    JPLocation addWithRotation(const JPLocation& l) const;
    JPLocation multiply(double x, double y, double z, double rotation) const;
    JPLocation multiply(double factor) const;
    JPLocation invert(bool x, bool y, bool z, bool rotation) const;
    // Any not given stays as it is.
    JPLocation derive(std::optional<double> x, std::optional<double> y, std::optional<double> z,
                      std::optional<double> rotation) const;
    JPLocation deriveLengths(std::optional<JPLength> x, std::optional<JPLength> y, std::optional<JPLength> z,
                             std::optional<double> rotation) const;
    // Those chosen taken from `l`.
    JPLocation derive(const JPLocation& l, bool x, bool y, bool z, bool rotation) const;
    // X and Y turned about the origin by `angle` degrees.
    JPLocation rotateXy(double angle) const;
    JPLocation rotateXyCenterPoint(const JPLocation& center, double angle) const;
    JPLocation offsetWithRotationFrom(const JPLocation& base) const;
    JPLocation localLocationRelativeTo(const JPLocation& base) const;
    // Within the rectangle this location's X and Y span from `origin`.
    bool containsLocation(const JPLocation& origin, const JPLocation& target) const;
    // Not the origin.
    bool isInitialized() const { return !(*this == origin()); }

    bool operator==(const JPLocation& o) const;
    std::string text() const;

private:
    JPLengthUnit m_units = JPLengthUnit::Millimeters;
    double       m_x = 0, m_y = 0, m_z = 0, m_rotation = 0;
};

} // inline namespace jf
