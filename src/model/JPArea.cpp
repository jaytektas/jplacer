// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPArea.h"

#include "JPVolume.h"
#include "JPAreaUnits.h"
#include "JPUnitText.h"
#include "JPVolumeUnits.h"

#include <cmath>
#include <cstdio>

inline namespace jf {

double JPArea::convert(double value, JPAreaUnit from, JPAreaUnit to) {
    if (from == to) return value;
    // As OpenPnP: the ratio of the two length units, to the power.
    const JPLength one(1.0, JPAreaUnits::lengthUnit(from));
    const double scale = std::pow(one.divide(JPLength(1.0, JPAreaUnits::lengthUnit(to))), 2);
    return value * scale;
}

JPArea JPArea::convertToUnits(JPAreaUnit units) const {
    if (m_units == units) return *this;
    return JPArea(convert(m_value, this->units(), units), units);
}

JPArea JPArea::add(const JPArea& area) const { return withValue(m_value + area.convertToUnits(units()).m_value); }
JPArea JPArea::subtract(const JPArea& area) const { return withValue(m_value - area.convertToUnits(units()).m_value); }
double JPArea::divide(const JPArea& area) const { return m_value / area.convertToUnits(units()).m_value; }
JPArea JPArea::modulo(const JPArea& area) const { return withValue(std::fmod(m_value, area.convertToUnits(units()).m_value)); }

JPVolume JPArea::multiply(const JPLength& length) const {
    const JPLengthUnit u = JPAreaUnits::lengthUnit(units());
    return JPVolume(m_value * length.convertToUnits(u).value(), JPVolumeUnits::fromLengthUnit(u));
}

JPLength JPArea::divide(const JPLength& length) const {
    const JPLengthUnit u = JPAreaUnits::lengthUnit(units());
    return JPLength(m_value / length.convertToUnits(u).value(), u);
}

std::optional<JPArea> JPArea::parse(const std::string& s, bool requireUnits) {
    const JPUnitText::Parts parts = JPUnitText::split(s, 2);
    std::optional<JPAreaUnit> units;
    if (parts.hasUnits)
        for (const JPAreaUnit u : JPAreaUnits::all())
            if (JPUnitText::sameIgnoringCase(JPAreaUnits::shortName(u), parts.units)) {
                units = u;
                break;
            }
    if (requireUnits && !units) return std::nullopt;
    const std::optional<double> v = JPUnitText::number(parts.value);
    if (!v) return std::nullopt;
    JPArea a;
    a.m_value = *v;
    a.m_units = units;
    return a;
}

std::string JPArea::text() const { return text("%2.3f%s"); }

std::string JPArea::text(const char* fmt) const {
    char buf[96];
    std::snprintf(buf, sizeof buf, fmt, m_value, JPAreaUnits::shortName(units()));
    return buf;
}

int JPArea::compare(const JPArea& other) const {
    const double o = other.convertToUnits(units()).m_value;
    return m_value < o ? -1 : m_value > o ? 1 : 0;
}

} // inline namespace jf
