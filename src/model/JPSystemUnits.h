// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPLengthUnit.h"

inline namespace jf {

// OpenPnP's System Units (View > System Units): the units every length is
// shown and typed in, Millimeters or Inches. Kept in millimetres whatever is
// chosen; chosen at start (a change takes effect on the next start, as
// OpenPnP's does).
class JPSystemUnits {
public:
    static JPLengthUnit units();
    static void setUnits(JPLengthUnit units);
    static bool inches() { return units() == JPLengthUnit::Inches; }
    // A length in millimetres as shown; and one shown, in millimetres.
    static double shown(double mm);
    static double stored(double value);
    // Places to show a length to: as many as in millimetres, one more in inches.
    static int places(int mmPlaces) { return inches() ? mmPlaces + 1 : mmPlaces; }
    // Its short name ("mm", "in"), for labels.
    static const char* suffix();
};

} // inline namespace jf
