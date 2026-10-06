// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPVolumeUnit.h"
#include "JPLength.h"

#include <optional>
#include <string>

inline namespace jf {

class JPArea;

// A volume and its units, as OpenPnP's Volume: converted (as the cube of its length unit), added,
// divided by a length into an area or by an area into a length; parsed from text such as "15μl" or "2 mm3".
class JPVolume {
public:
    JPVolume() = default;
    JPVolume(double value, JPVolumeUnit units) : m_value(value), m_units(units) {}

    double     value() const { return m_value; }
    JPVolumeUnit units() const { return m_units.value_or(kDefaultUnits); }
    bool       hasUnits() const { return m_units.has_value(); }

    JPVolume convertToUnits(JPVolumeUnit units) const;
    static double convert(double value, JPVolumeUnit from, JPVolumeUnit to);
    JPVolume add(const JPVolume& volume) const;
    JPVolume subtract(const JPVolume& volume) const;
    JPVolume add(double d) const { return withValue(m_value + d); }
    JPVolume subtract(double d) const { return withValue(m_value - d); }
    JPVolume multiply(double d) const { return withValue(m_value * d); }
    JPVolume divide(double d) const { return withValue(m_value / d); }
    double divide(const JPVolume& volume) const;
    // In the square of this volume's length unit.
    JPArea   divide(const JPLength& length) const;
    // In this volume's length unit.
    JPLength divide(const JPArea& area) const;
    JPVolume modulo(const JPVolume& volume) const;

    // A number and, unless `requireUnits`, perhaps a unit's short name after it ("u" for "μ", "3" for its
    // superscript); nothing when it is not one.
    static std::optional<JPVolume> parse(const std::string& s, bool requireUnits = false);

    // "%2.3f" and the unit's short name, as OpenPnP shows it.
    std::string text() const;
    // `fmt`: printf with %f then %s.
    std::string text(const char* fmt) const;

    int  compare(const JPVolume& other) const;
    bool operator==(const JPVolume& o) const { return m_value == o.m_value && m_units == o.m_units; }

private:
    static constexpr JPVolumeUnit kDefaultUnits = JPVolumeUnit::MicroLiters;

    JPVolume withValue(double v) const {
        JPVolume a = *this;
        a.m_value = v;
        return a;
    }

    double                    m_value = 0;
    std::optional<JPVolumeUnit> m_units;
};

} // inline namespace jf
