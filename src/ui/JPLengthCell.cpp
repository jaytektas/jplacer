// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPLengthCell.h"

#include "model/JPLengthUnits.h"

#include <cstdio>

inline namespace jf {

namespace {
// OpenPnP's system units, and its length formats (Configuration's defaults).
constexpr JPLengthUnit kSystemUnits = JPLengthUnit::Millimeters;
constexpr const char*  kFormat = "%.3f";
constexpr const char*  kFormatWithUnits = "%.3f%s";
}

std::string JPLengthCell::text(const JPLength& l, bool nativeUnits) {
    char buf[64];
    if (nativeUnits && l.units() != kSystemUnits) {
        std::snprintf(buf, sizeof buf, kFormatWithUnits, l.value(), JPLengthUnits::shortName(l.units()));
        return buf;
    }
    std::snprintf(buf, sizeof buf, kFormat, l.convertToUnits(kSystemUnits).value());
    return buf;
}

bool JPLengthCell::parse(const std::string& text, const JPLength& old, JPLength& out) {
    const auto l = JPLength::parse(text);
    if (!l) return false;
    out = l->changeUnitsIfUnspecified(old.units()).changeUnitsIfUnspecified(kSystemUnits);
    return true;
}

} // inline namespace jf
