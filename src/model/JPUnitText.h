// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <optional>
#include <string>

inline namespace jf {

// How OpenPnP's Length, Area and Volume read text: a number (digits, '-' and '.') and, after it, perhaps a unit's
// short name, its case ignored.
struct JPUnitText {
    struct Parts {
        std::string value;   // the number's text
        std::string units;   // the unit's text, trimmed; "u" read as "μ", `power` as its superscript
        bool        hasUnits = false;
    };
    // `power`: 2 or 3, the digit read as "²" or "³" (as OpenPnP's Area and Volume do); 0 none.
    static Parts split(const std::string& text, int power = 0);
    // The number, as Java's Double.parseDouble reads it (surrounding blanks allowed); none when it is not one.
    static std::optional<double> number(const std::string& value);
    static bool sameIgnoringCase(const std::string& a, const std::string& b);
};

} // inline namespace jf
