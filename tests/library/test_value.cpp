// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// Part values written the many ways CAD tools and suppliers write them read
// as one number with its unit.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "library/JPValue.h"

#include <cmath>

using namespace jf;

namespace {

bool is(const char* text, double number, JPValue::Unit unit) {
    JPValue v;
    return JPValue::parse(text, v) && std::abs(v.number - number) <= std::abs(number) * 1e-9 && v.unit == unit;
}

} // namespace

int main() {
    using U = JPValue::Unit;
    assert(is("100\xCE\xA9", 100, U::Ohm));          // Greek capital omega
    assert(is("100\xE2\x84\xA6", 100, U::Ohm));      // the ohm sign
    assert(is("100R", 100, U::Ohm));
    assert(is("100 ohm", 100, U::Ohm));
    assert(is("0.1k", 100, U::None));
    assert(is("4k7", 4700, U::None));
    assert(is("4R7", 4.7, U::Ohm));
    assert(is("0R", 0, U::Ohm));
    assert(is("1M", 1e6, U::None));
    assert(is("4M7", 4.7e6, U::None));
    assert(is("100nF", 100e-9, U::Farad));
    assert(is("0.1uF", 100e-9, U::Farad));
    assert(is("0.1\xC2\xB5" "F", 100e-9, U::Farad));   // micro sign
    assert(is("2n2", 2.2e-9, U::None));
    assert(is("10uH", 10e-6, U::Henry));
    assert(is("100mF", 0.1, U::Farad));

    JPValue v;
    assert(!JPValue::parse("STM32F103", v));
    assert(!JPValue::parse("LED", v));
    assert(!JPValue::parse("", v));

    assert(JPValue::same("100\xCE\xA9", "100R"));
    assert(JPValue::same("100nF", "0.1uF"));
    assert(!JPValue::same("100nF", "100n"));          // a unit against none
    assert(!JPValue::same("10k", "1k"));
    assert(JPValue::same("VNQ7140AJTR", "vnq7140ajtr"));   // not values: the text, case aside
    assert(JPValue::same("\xC2\xB1" "1%", "\xC2\xB1" "1%"));
    return 0;
}
