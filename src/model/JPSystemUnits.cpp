// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPSystemUnits.h"

#include "JPLengthUnits.h"

#include <atomic>

inline namespace jf {

namespace {
std::atomic<JPLengthUnit> s_units { JPLengthUnit::Millimeters };
}

JPLengthUnit JPSystemUnits::units() {
    return s_units.load();
}

void JPSystemUnits::setUnits(JPLengthUnit units) {
    s_units = units == JPLengthUnit::Inches ? JPLengthUnit::Inches : JPLengthUnit::Millimeters;
}

double JPSystemUnits::shown(double mm) {
    return mm / JPLengthUnits::toMillimeters(units());
}

double JPSystemUnits::stored(double value) {
    return value * JPLengthUnits::toMillimeters(units());
}

const char* JPSystemUnits::suffix() {
    return inches() ? "in" : "mm";
}

} // inline namespace jf
