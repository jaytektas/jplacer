// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPOrigin.h"

#include "job/JPSupplierNumber.h"

#include <j/config/Json.h>

#include <string>
#include <vector>

inline namespace jf {

// One thing that can be bought and placed, in exactly one package: an MPN
// names its package, so the same device in another package is another part.
// It is known by its manufacturer and MPN where they are given; a generic
// part (a 100 nF 0402 X7R capacitor, whoever makes it) by its value,
// ratings and package together. Nothing physical: sizes are its package's.
// Its id is jplacer's own, never made from its fields.
struct JPPart {
    std::string                   id;
    std::string                   mpn;
    std::string                   manufacturer;
    std::vector<JPSupplierNumber> supplierNumbers;
    std::string                   value;
    std::string                   tolerance, voltage, power, dielectric, temperature;
    std::string                   description;
    std::string                   packageId;   // empty: none yet (not placeable)
    int                           revision = 1;
    JPOrigin                      origin;

    // What to call it: its MPN, else its value (the package is shown beside).
    std::string label() const { return !mpn.empty() ? mpn : value; }
    bool hasNumber(const JPSupplierNumber& n) const;

    static JPPart fromJson(const JJson& j);
    JJson toJson() const;
};

} // inline namespace jf
