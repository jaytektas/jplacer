// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPLengthUnits.h"

inline namespace jf {

namespace {

struct Row {
    JPLengthUnit u;
    const char*  name;
    const char*  shortName;
    const char*  singular;
    double       mm;
};

constexpr Row kRows[] = {
    { JPLengthUnit::Meters,      "Meters",      "m",            "Meter",      1000 },
    { JPLengthUnit::Centimeters, "Centimeters", "cm",           "Centimeter", 10 },
    { JPLengthUnit::Millimeters, "Millimeters", "mm",           "Millimeter", 1 },
    { JPLengthUnit::Feet,        "Feet",        "'",            "Foot",       25.4 * 12 },
    { JPLengthUnit::Inches,      "Inches",      "\"",           "Inch",       25.4 },
    { JPLengthUnit::Mils,        "Mils",        "mil",          "Mil",        25.4 / 1000 },
    { JPLengthUnit::Microns,     "Microns",     "\xCE\xBCm",    "Micron",     1.0 / 1000 },
};

const Row& row(JPLengthUnit u) {
    for (const Row& r : kRows)
        if (r.u == u) return r;
    return kRows[2];
}

} // namespace

const char* JPLengthUnits::name(JPLengthUnit u) { return row(u).name; }
const char* JPLengthUnits::shortName(JPLengthUnit u) { return row(u).shortName; }
const char* JPLengthUnits::singularName(JPLengthUnit u) { return row(u).singular; }
double JPLengthUnits::toMillimeters(JPLengthUnit u) { return row(u).mm; }

bool JPLengthUnits::fromName(const std::string& s, JPLengthUnit& out) {
    for (const Row& r : kRows)
        if (s == r.name) {
            out = r.u;
            return true;
        }
    return false;
}

} // inline namespace jf
