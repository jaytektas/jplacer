// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPVolume.h"

#include "JPArea.h"
#include "JPAreaUnits.h"
#include "JPUnitText.h"
#include "JPVolumeUnits.h"

#include <cmath>
#include <cstdio>

inline namespace jf {

double JPVolume::convert(double value, JPVolumeUnit from, JPVolumeUnit to) {
    if (from == to) return value;
    // As OpenPnP: the ratio of the two length units, to the power.
    const JPLength one(1.0, JPVolumeUnits::lengthUnit(from));
    const double scale = std::pow(one.divide(JPLength(1.0, JPVolumeUnits::lengthUnit(to))), 3);
    return value * scale;
}

JPVolume JPVolume::convertToUnits(JPVolumeUnit units) const {
    if (m_units == units) return *this;
    return JPVolume(convert(m_value, this->units(), units), units);
}

JPVolume JPVolume::add(const JPVolume& volume) const { return withValue(m_value + volume.convertToUnits(units()).m_value); }
JPVolume JPVolume::subtract(const JPVolume& volume) const { return withValue(m_value - volume.convertToUnits(units()).m_value); }
double JPVolume::divide(const JPVolume& volume) const { return m_value / volume.convertToUnits(units()).m_value; }
JPVolume JPVolume::modulo(const JPVolume& volume) const { return withValue(std::fmod(m_value, volume.convertToUnits(units()).m_value)); }

JPArea JPVolume::divide(const JPLength& length) const {
    const JPLengthUnit u = JPVolumeUnits::lengthUnit(units());
    return JPArea(m_value / length.convertToUnits(u).value(), JPAreaUnits::fromLengthUnit(u));
}

JPLength JPVolume::divide(const JPArea& area) const {
    const JPLengthUnit u = JPVolumeUnits::lengthUnit(units());
    return JPLength(m_value / area.convertToUnits(JPAreaUnits::fromLengthUnit(u)).value(), u);
}

std::optional<JPVolume> JPVolume::parse(const std::string& s, bool requireUnits) {
    const JPUnitText::Parts parts = JPUnitText::split(s, 3);
    std::optional<JPVolumeUnit> units;
    if (parts.hasUnits)
        for (const JPVolumeUnit u : JPVolumeUnits::all())
            if (JPUnitText::sameIgnoringCase(JPVolumeUnits::shortName(u), parts.units)) {
                units = u;
                break;
            }
    if (requireUnits && !units) return std::nullopt;
    const std::optional<double> v = JPUnitText::number(parts.value);
    if (!v) return std::nullopt;
    JPVolume a;
    a.m_value = *v;
    a.m_units = units;
    return a;
}

std::string JPVolume::text() const { return text("%2.3f%s"); }

std::string JPVolume::text(const char* fmt) const {
    char buf[96];
    std::snprintf(buf, sizeof buf, fmt, m_value, JPVolumeUnits::shortName(units()));
    return buf;
}

int JPVolume::compare(const JPVolume& other) const {
    const double o = other.convertToUnits(units()).m_value;
    return m_value < o ? -1 : m_value > o ? 1 : 0;
}

} // inline namespace jf
