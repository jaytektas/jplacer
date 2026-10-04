// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <string>

inline namespace jf {

// A part's value read as a number with its unit, so the ways CAD tools and
// suppliers write one value compare equal: "100Ω", "100R", "100 ohm", "0.1k";
// "4k7" and "4R7" (the letter as the decimal point); "100nF", "0.1uF",
// "100n"; "10µH". An M with no unit after it is mega, as resistors are
// written ("1M", "4M7"). A value with no unit has none (Unit::None), and
// compares only with another number written without one.
struct JPValue {
    enum class Unit { None, Ohm, Farad, Henry };

    double number = 0;
    Unit   unit = Unit::None;

    // False when the text is not a number of this kind ("STM32F103", "LED").
    static bool parse(const std::string& text, JPValue& out);
    // The two texts are one value: equal numbers with the same unit, or, when
    // either is not a value, the same text (case aside).
    static bool same(const std::string& a, const std::string& b);
};

} // inline namespace jf
