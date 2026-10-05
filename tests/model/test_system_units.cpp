// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// OpenPnP's System Units: lengths kept in millimetres, shown and typed in
// millimetres or inches (one more place in inches); nothing else is taken.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "model/JPSystemUnits.h"

#include <cmath>
#include <cstring>

using namespace jf;

int main() {
    assert(JPSystemUnits::units() == JPLengthUnit::Millimeters && JPSystemUnits::shown(12.5) == 12.5);
    assert(JPSystemUnits::places(3) == 3 && std::strcmp(JPSystemUnits::suffix(), "mm") == 0);
    JPSystemUnits::setUnits(JPLengthUnit::Inches);
    assert(JPSystemUnits::inches());
    assert(std::abs(JPSystemUnits::shown(25.4) - 1) < 1e-12 && std::abs(JPSystemUnits::stored(2) - 50.8) < 1e-12);
    assert(JPSystemUnits::places(3) == 4 && std::strcmp(JPSystemUnits::suffix(), "in") == 0);
    JPSystemUnits::setUnits(JPLengthUnit::Mils);   // only the two OpenPnP offers
    assert(JPSystemUnits::units() == JPLengthUnit::Millimeters);
    return 0;
}
