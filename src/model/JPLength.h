// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPLengthUnit.h"

#include <optional>
#include <string>

inline namespace jf {

class JPArea;

// A length and its units, as OpenPnP's Length: converted, added and
// compared across units; parsed from text such as "1.5mm" or "20 mil".
class JPLength {
public:
    JPLength() = default;
    JPLength(double value, JPLengthUnit units) : m_value(value), m_units(units) {}

    double       value() const { return m_value; }
    JPLengthUnit units() const { return m_units.value_or(JPLengthUnit::Millimeters); }
    bool         hasUnits() const { return m_units.has_value(); }

    JPLength convertToUnits(JPLengthUnit units) const;
    static double convert(double value, JPLengthUnit from, JPLengthUnit to);
    JPLength add(const JPLength& l) const;
    JPLength subtract(const JPLength& l) const;
    JPLength add(double d) const { return withValue(m_value + d); }
    JPLength subtract(double d) const { return withValue(m_value - d); }
    JPLength multiply(double d) const { return withValue(m_value * d); }
    // The area of this times `l`, in the square of these units.
    JPArea   multiply(const JPLength& l) const;
    JPLength modulo(const JPLength& l) const;
    JPLength divide(double d) const { return withValue(m_value / d); }
    double   divide(const JPLength& l) const { return m_value / l.convertToUnits(units()).m_value; }
    JPLength abs() const;
    // The units, where none were given.
    JPLength changeUnitsIfUnspecified(JPLengthUnit units) const;

    // A number and, unless `requireUnits`, perhaps a unit's short name after
    // it ("u" for "μ"); nothing when it is not one.
    static std::optional<JPLength> parse(const std::string& s, bool requireUnits = false);
    static std::optional<JPLength> parseWithDefaultUnits(const std::string& s, JPLengthUnit units);

    // "%2.3f" and the unit's short name, as OpenPnP shows a length.
    std::string text() const;
    // `fmt`: printf with %f then %s.
    std::string text(const char* fmt) const;

    int  compare(const JPLength& other) const;
    bool operator==(const JPLength& o) const;

private:
    JPLength withValue(double v) const {
        JPLength l = *this;
        l.m_value = v;
        return l;
    }

    double                      m_value = 0;
    std::optional<JPLengthUnit> m_units;
};

} // inline namespace jf
