// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPVolumeUnits.h"

inline namespace jf {

namespace {

struct Row {
    JPVolumeUnit u;
    const char*  name;
    const char*  shortName;
    const char*  singular;
    JPLengthUnit length;
};

constexpr Row kRows[] = {
    { JPVolumeUnit::CubicMeters,      "CubicMeters",      "m\xC2\xB3",         "CubicMeter",      JPLengthUnit::Meters },
    { JPVolumeUnit::CubicCentimeters, "CubicCentimeters", "cm\xC2\xB3",        "CubicCentimeter", JPLengthUnit::Centimeters },
    { JPVolumeUnit::MilliLiters,      "MilliLiters",      "ml",                "MilliLiter",      JPLengthUnit::Centimeters },
    { JPVolumeUnit::CubicMillimeters, "CubicMillimeters", "mm\xC2\xB3",        "CubicMillimeter", JPLengthUnit::Millimeters },
    { JPVolumeUnit::MicroLiters,      "MicroLiters",      "\xCE\xBCl",         "MicroLiter",      JPLengthUnit::Millimeters },
    { JPVolumeUnit::CubicFeet,        "CubicFeet",        "ft\xC2\xB3",        "CubicFoot",       JPLengthUnit::Feet },
    { JPVolumeUnit::CubicInches,      "CubicInches",      "in\xC2\xB3",        "CubicInch",       JPLengthUnit::Inches },
    { JPVolumeUnit::CubicMils,        "CubicMils",        "mil\xC2\xB3",       "CubicMil",        JPLengthUnit::Mils },
    { JPVolumeUnit::CubicMicrons,     "CubicMicrons",     "\xCE\xBCm\xC2\xB3", "CubicMicron",     JPLengthUnit::Microns },
    { JPVolumeUnit::FemtoLiters,      "FemtoLiters",      "fl",                "FemtoLiter",      JPLengthUnit::Microns },
};

const Row& row(JPVolumeUnit u) {
    for (const Row& r : kRows)
        if (r.u == u) return r;
    return kRows[4];
}

} // namespace

const std::vector<JPVolumeUnit>& JPVolumeUnits::all() {
    static const std::vector<JPVolumeUnit> units = [] {
        std::vector<JPVolumeUnit> v;
        for (const Row& r : kRows) v.push_back(r.u);
        return v;
    }();
    return units;
}

const char* JPVolumeUnits::name(JPVolumeUnit u) { return row(u).name; }
const char* JPVolumeUnits::shortName(JPVolumeUnit u) { return row(u).shortName; }
const char* JPVolumeUnits::singularName(JPVolumeUnit u) { return row(u).singular; }
JPLengthUnit JPVolumeUnits::lengthUnit(JPVolumeUnit u) { return row(u).length; }

JPVolumeUnit JPVolumeUnits::fromLengthUnit(JPLengthUnit u, bool preferred) {
    switch (u) {
        case JPLengthUnit::Meters:      return JPVolumeUnit::CubicMeters;
        case JPLengthUnit::Centimeters: return preferred ? JPVolumeUnit::MilliLiters : JPVolumeUnit::CubicCentimeters;
        case JPLengthUnit::Millimeters: return preferred ? JPVolumeUnit::MicroLiters : JPVolumeUnit::CubicMillimeters;
        case JPLengthUnit::Feet:        return JPVolumeUnit::CubicFeet;
        case JPLengthUnit::Inches:      return JPVolumeUnit::CubicInches;
        case JPLengthUnit::Mils:        return JPVolumeUnit::CubicMils;
        case JPLengthUnit::Microns:     return preferred ? JPVolumeUnit::FemtoLiters : JPVolumeUnit::CubicMicrons;
    }
    return JPVolumeUnit::MicroLiters;
}

bool JPVolumeUnits::fromName(const std::string& s, JPVolumeUnit& out) {
    for (const Row& r : kRows)
        if (s == r.name) {
            out = r.u;
            return true;
        }
    return false;
}

} // inline namespace jf
