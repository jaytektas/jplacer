// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPAreaUnits.h"

inline namespace jf {

namespace {

struct Row {
    JPAreaUnit   u;
    const char*  name;
    const char*  shortName;
    const char*  singular;
    JPLengthUnit length;
};

constexpr Row kRows[] = {
    { JPAreaUnit::SquareMeters,      "SquareMeters",      "m\xC2\xB2",         "SquareMeter",      JPLengthUnit::Meters },
    { JPAreaUnit::SquareCentimeters, "SquareCentimeters", "cm\xC2\xB2",        "SquareCentimeter", JPLengthUnit::Centimeters },
    { JPAreaUnit::SquareMillimeters, "SquareMillimeters", "mm\xC2\xB2",        "SquareMillimeter", JPLengthUnit::Millimeters },
    { JPAreaUnit::SquareFeet,        "SquareFeet",        "ft\xC2\xB2",        "SquareFoot",       JPLengthUnit::Feet },
    { JPAreaUnit::SquareInches,      "SquareInches",      "in\xC2\xB2",        "SquareInch",       JPLengthUnit::Inches },
    { JPAreaUnit::SquareMils,        "SquareMils",        "mil\xC2\xB2",       "SquareMil",        JPLengthUnit::Mils },
    { JPAreaUnit::SquareMicrons,     "SquareMicrons",     "\xCE\xBCm\xC2\xB2", "SquareMicron",     JPLengthUnit::Microns },
};

const Row& row(JPAreaUnit u) {
    for (const Row& r : kRows)
        if (r.u == u) return r;
    return kRows[2];
}

} // namespace

const std::vector<JPAreaUnit>& JPAreaUnits::all() {
    static const std::vector<JPAreaUnit> units = [] {
        std::vector<JPAreaUnit> v;
        for (const Row& r : kRows) v.push_back(r.u);
        return v;
    }();
    return units;
}

const char* JPAreaUnits::name(JPAreaUnit u) { return row(u).name; }
const char* JPAreaUnits::shortName(JPAreaUnit u) { return row(u).shortName; }
const char* JPAreaUnits::singularName(JPAreaUnit u) { return row(u).singular; }
JPLengthUnit JPAreaUnits::lengthUnit(JPAreaUnit u) { return row(u).length; }

JPAreaUnit JPAreaUnits::fromLengthUnit(JPLengthUnit u) {
    for (const Row& r : kRows)
        if (r.length == u) return r.u;
    return JPAreaUnit::SquareMillimeters;
}

bool JPAreaUnits::fromName(const std::string& s, JPAreaUnit& out) {
    for (const Row& r : kRows)
        if (s == r.name) {
            out = r.u;
            return true;
        }
    return false;
}

} // inline namespace jf
