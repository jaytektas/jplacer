// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPLength.h"

#include "JPArea.h"
#include "JPAreaUnits.h"
#include "JPLengthUnits.h"
#include "JPUnitText.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

inline namespace jf {

namespace {

// As OpenPnP's Length.convertToUnits, step for step, so the numbers come out
// the same to the last bit.
double toMm(double v, JPLengthUnit u) {
    switch (u) {
        case JPLengthUnit::Millimeters: return v;
        case JPLengthUnit::Centimeters: return v * 10;
        case JPLengthUnit::Meters:      return v * 1000;
        case JPLengthUnit::Inches:      return v * 25.4;
        case JPLengthUnit::Feet:        return v * 25.4 * 12;
        case JPLengthUnit::Mils:        return v / 1000 * 25.4;
        case JPLengthUnit::Microns:     return v / 1000.0;
    }
    return v;
}

double fromMm(double mm, JPLengthUnit u) {
    switch (u) {
        case JPLengthUnit::Millimeters: return mm;
        case JPLengthUnit::Centimeters: return mm / 10;
        case JPLengthUnit::Meters:      return mm / 1000;
        case JPLengthUnit::Inches:      return mm * (1 / 25.4);
        case JPLengthUnit::Feet:        return mm * (1 / (25.4 * 12));
        case JPLengthUnit::Mils:        return mm * (1 / 25.4 * 1000);
        case JPLengthUnit::Microns:     return mm * 1000;
    }
    return mm;
}

} // namespace

double JPLength::convert(double value, JPLengthUnit from, JPLengthUnit to) {
    if (from == to) return value;
    return fromMm(toMm(value, from), to);
}

JPLength JPLength::convertToUnits(JPLengthUnit units) const {
    if (m_units == units) return *this;
    return JPLength(convert(m_value, this->units(), units), units);
}

JPLength JPLength::add(const JPLength& l) const {
    return withValue(m_value + l.convertToUnits(units()).m_value);
}

JPLength JPLength::subtract(const JPLength& l) const {
    return withValue(m_value - l.convertToUnits(units()).m_value);
}

JPLength JPLength::modulo(const JPLength& l) const {
    return withValue(std::fmod(m_value, l.convertToUnits(units()).m_value));
}

JPArea JPLength::multiply(const JPLength& l) const {
    return JPArea(m_value * l.convertToUnits(units()).m_value, JPAreaUnits::fromLengthUnit(units()));
}

JPLength JPLength::abs() const {
    return withValue(std::fabs(m_value));
}

JPLength JPLength::changeUnitsIfUnspecified(JPLengthUnit units) const {
    if (m_units) return *this;
    return JPLength(m_value, units);
}

std::optional<JPLength> JPLength::parse(const std::string& text, bool requireUnits) {
    const JPUnitText::Parts parts = JPUnitText::split(text);
    std::optional<JPLengthUnit> units;
    if (parts.hasUnits)
        for (const JPLengthUnit u : { JPLengthUnit::Meters, JPLengthUnit::Centimeters, JPLengthUnit::Millimeters,
                                      JPLengthUnit::Feet, JPLengthUnit::Inches, JPLengthUnit::Mils, JPLengthUnit::Microns })
            if (JPUnitText::sameIgnoringCase(JPLengthUnits::shortName(u), parts.units)) {
                units = u;
                break;
            }
    if (requireUnits && !units) return std::nullopt;
    const std::optional<double> v = JPUnitText::number(parts.value);
    if (!v) return std::nullopt;
    JPLength l;
    l.m_value = *v;
    l.m_units = units;
    return l;
}

std::optional<JPLength> JPLength::parseWithDefaultUnits(const std::string& s, JPLengthUnit units) {
    auto l = parse(s);
    if (l) return l->changeUnitsIfUnspecified(units);
    return l;
}

std::string JPLength::text() const {
    return text("%2.3f%s");
}

std::string JPLength::text(const char* fmt) const {
    char buf[96];
    std::snprintf(buf, sizeof buf, fmt, m_value, JPLengthUnits::shortName(units()));
    return buf;
}

int JPLength::compare(const JPLength& other) const {
    const double o = other.convertToUnits(units()).m_value;
    return m_value < o ? -1 : m_value > o ? 1 : 0;
}

bool JPLength::operator==(const JPLength& o) const {
    return m_value == o.m_value && m_units == o.m_units;
}

} // inline namespace jf
