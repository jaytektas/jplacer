// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "model/JPLength.h"

#include <string>

inline namespace jf {

// A length in a table cell, as OpenPnP's LengthCellValue: shown to three
// places in the System Units (JPSystemUnits), or in its own units with
// their name when `nativeUnits` and they differ; edited as a number with or
// without units, the old length's units taken where none are given.
struct JPLengthCell {
    static std::string text(const JPLength& l, bool nativeUnits);
    // False when `text` is not a length.
    static bool parse(const std::string& text, const JPLength& old, JPLength& out);
};

} // inline namespace jf
