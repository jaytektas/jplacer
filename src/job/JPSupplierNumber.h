// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <string>

inline namespace jf {

// A supplier's order number for a part: unique within that supplier
// ("LCSC" "C17408", "Digi-Key" "311-100CRCT-ND"). An empty supplier is one
// the file did not name.
struct JPSupplierNumber {
    std::string supplier;
    std::string number;

    bool operator==(const JPSupplierNumber&) const = default;
};

} // inline namespace jf
