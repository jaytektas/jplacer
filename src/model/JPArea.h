// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPAreaUnit.h"
#include "JPLength.h"

#include <optional>
#include <string>

inline namespace jf {

class JPVolume;

// An area and its units, as OpenPnP's Area: converted (as the square of its length unit), added,
// multiplied by a length into a volume, divided by one into a length; parsed from text such as "1.5mm²" or "2 mm2".
class JPArea {
public:
    JPArea() = default;
    JPArea(double value, JPAreaUnit units) : m_value(value), m_units(units) {}

    double     value() const { return m_value; }
    JPAreaUnit units() const { return m_units.value_or(kDefaultUnits); }
    bool       hasUnits() const { return m_units.has_value(); }

    JPArea convertToUnits(JPAreaUnit units) const;
    static double convert(double value, JPAreaUnit from, JPAreaUnit to);
    JPArea add(const JPArea& area) const;
    JPArea subtract(const JPArea& area) const;
    JPArea add(double d) const { return withValue(m_value + d); }
    JPArea subtract(double d) const { return withValue(m_value - d); }
    JPArea multiply(double d) const { return withValue(m_value * d); }
    JPArea divide(double d) const { return withValue(m_value / d); }
    double divide(const JPArea& area) const;
    // In the cube of this area's length unit (its litre name where it has one).
    JPVolume multiply(const JPLength& length) const;
    // In this area's length unit.
    JPLength divide(const JPLength& length) const;
    JPArea modulo(const JPArea& area) const;

    // A number and, unless `requireUnits`, perhaps a unit's short name after it ("u" for "μ", "2" for its
    // superscript); nothing when it is not one.
    static std::optional<JPArea> parse(const std::string& s, bool requireUnits = false);

    // "%2.3f" and the unit's short name, as OpenPnP shows it.
    std::string text() const;
    // `fmt`: printf with %f then %s.
    std::string text(const char* fmt) const;

    int  compare(const JPArea& other) const;
    bool operator==(const JPArea& o) const { return m_value == o.m_value && m_units == o.m_units; }

private:
    static constexpr JPAreaUnit kDefaultUnits = JPAreaUnit::SquareMillimeters;

    JPArea withValue(double v) const {
        JPArea a = *this;
        a.m_value = v;
        return a;
    }

    double                    m_value = 0;
    std::optional<JPAreaUnit> m_units;
};

} // inline namespace jf
