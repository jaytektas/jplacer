// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPLocation.h"

#include "JPLengthUnits.h"

#include <cmath>
#include <cstdio>

inline namespace jf {

JPLocation JPLocation::convertToUnits(JPLengthUnit units) const {
    if (m_units == units) return *this;
    return JPLocation(units, JPLength::convert(m_x, m_units, units), JPLength::convert(m_y, m_units, units),
                      JPLength::convert(m_z, m_units, units), m_rotation);
}

double JPLocation::linearDistanceTo(const JPLocation& l) const {
    const JPLocation o = l.convertToUnits(m_units);
    return linearDistanceTo(o.m_x, o.m_y);
}

double JPLocation::linearDistanceTo(double x, double y) const {
    return std::sqrt(std::pow(m_x - x, 2) + std::pow(m_y - y, 2));
}

double JPLocation::xyzDistanceTo(const JPLocation& l) const {
    const JPLocation o = l.convertToUnits(m_units);
    return std::sqrt(std::pow(m_x - o.m_x, 2) + std::pow(m_y - o.m_y, 2) + std::pow(m_z - o.m_z, 2));
}

JPLocation JPLocation::subtract(const JPLocation& l) const {
    const JPLocation o = l.convertToUnits(m_units);
    return JPLocation(o.m_units, m_x - o.m_x, m_y - o.m_y, m_z - o.m_z, m_rotation);
}

JPLocation JPLocation::subtractWithRotation(const JPLocation& l) const {
    const JPLocation o = l.convertToUnits(m_units);
    return JPLocation(o.m_units, m_x - o.m_x, m_y - o.m_y, m_z - o.m_z, m_rotation - o.m_rotation);
}

JPLocation JPLocation::add(const JPLocation& l) const {
    const JPLocation o = l.convertToUnits(m_units);
    return JPLocation(o.m_units, m_x + o.m_x, m_y + o.m_y, m_z + o.m_z, m_rotation);
}

JPLocation JPLocation::addWithRotation(const JPLocation& l) const {
    const JPLocation o = l.convertToUnits(m_units);
    return JPLocation(o.m_units, m_x + o.m_x, m_y + o.m_y, m_z + o.m_z, m_rotation + o.m_rotation);
}

JPLocation JPLocation::multiply(double x, double y, double z, double rotation) const {
    return JPLocation(m_units, x * m_x, y * m_y, z * m_z, rotation * m_rotation);
}

JPLocation JPLocation::multiply(double f) const {
    return JPLocation(m_units, f * m_x, f * m_y, f * m_z, f * m_rotation);
}

JPLocation JPLocation::invert(bool x, bool y, bool z, bool rotation) const {
    return JPLocation(m_units, m_x * (x ? -1 : 1), m_y * (y ? -1 : 1), m_z * (z ? -1 : 1), m_rotation * (rotation ? -1 : 1));
}

JPLocation JPLocation::derive(std::optional<double> x, std::optional<double> y, std::optional<double> z,
                              std::optional<double> rotation) const {
    return JPLocation(m_units, x.value_or(m_x), y.value_or(m_y), z.value_or(m_z), rotation.value_or(m_rotation));
}

JPLocation JPLocation::deriveLengths(std::optional<JPLength> x, std::optional<JPLength> y, std::optional<JPLength> z,
                                     std::optional<double> rotation) const {
    return JPLocation(m_units, x ? x->convertToUnits(m_units).value() : m_x, y ? y->convertToUnits(m_units).value() : m_y,
                      z ? z->convertToUnits(m_units).value() : m_z, rotation.value_or(m_rotation));
}

JPLocation JPLocation::derive(const JPLocation& l, bool x, bool y, bool z, bool rotation) const {
    const JPLocation o = l.convertToUnits(m_units);
    return JPLocation(m_units, x ? o.m_x : m_x, y ? o.m_y : m_y, z ? o.m_z : m_z, rotation ? o.m_rotation : m_rotation);
}

JPLocation JPLocation::rotateXy(double angle) const {
    if (angle == 0.0) return *this;
    while (angle < 180.) angle += 360;
    while (angle > 180.) angle -= 360;
    angle = angle * M_PI / 180;
    return JPLocation(m_units, m_x * std::cos(angle) - m_y * std::sin(angle), m_x * std::sin(angle) + m_y * std::cos(angle),
                      m_z, m_rotation);
}

JPLocation JPLocation::rotateXyCenterPoint(const JPLocation& center, double angle) const {
    return subtract(center).rotateXy(angle).add(center);
}

JPLocation JPLocation::offsetWithRotationFrom(const JPLocation& base) const {
    return base.addWithRotation(rotateXy(base.rotation()));
}

JPLocation JPLocation::localLocationRelativeTo(const JPLocation& base) const {
    return subtractWithRotation(base).rotateXy(-base.rotation());
}

bool JPLocation::containsLocation(const JPLocation& originLocation, const JPLocation& targetLocation) const {
    const JPLocation t = targetLocation.convertToUnits(m_units);
    const JPLocation o = originLocation.convertToUnits(m_units);
    const double x1 = o.m_x, y1 = o.m_y, x2 = x1 + m_x, y2 = y1 + m_y;
    return t.m_x >= x1 && t.m_x <= x2 && t.m_y > y1 && t.m_y < y2;
}

bool JPLocation::operator==(const JPLocation& other) const {
    const JPLocation o = other.convertToUnits(m_units);
    return m_units == o.m_units && m_x == o.m_x && m_y == o.m_y && m_z == o.m_z && m_rotation == o.m_rotation;
}

std::string JPLocation::text() const {
    char buf[160];
    std::snprintf(buf, sizeof buf, "(%f, %f, %f, %f %s)", m_x, m_y, m_z, m_rotation, JPLengthUnits::shortName(m_units));
    return buf;
}

} // inline namespace jf
