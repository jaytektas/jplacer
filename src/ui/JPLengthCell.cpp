// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPLengthCell.h"

#include "model/JPLengthUnits.h"
#include "model/JPSystemUnits.h"

#include <cstdio>

inline namespace jf {

namespace {
// OpenPnP's length formats (Configuration's defaults); the units are the System Units.
constexpr const char*  kFormat = "%.3f";
constexpr const char*  kFormatWithUnits = "%.3f%s";
}

std::string JPLengthCell::text(const JPLength& l, bool nativeUnits) {
    char buf[64];
    if (nativeUnits && l.units() != JPSystemUnits::units()) {
        std::snprintf(buf, sizeof buf, kFormatWithUnits, l.value(), JPLengthUnits::shortName(l.units()));
        return buf;
    }
    std::snprintf(buf, sizeof buf, kFormat, l.convertToUnits(JPSystemUnits::units()).value());
    return buf;
}

bool JPLengthCell::parse(const std::string& text, const JPLength& old, JPLength& out) {
    const auto l = JPLength::parse(text);
    if (!l) return false;
    out = l->changeUnitsIfUnspecified(old.units()).changeUnitsIfUnspecified(JPSystemUnits::units());
    return true;
}

} // inline namespace jf
