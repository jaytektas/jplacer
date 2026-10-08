// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPPackage.h"
#include "JPPart.h"

#include <j/config/Json.h>

#include <string>

inline namespace jf {

// A library part or package as JSON, whole: what the library keeps of it
// (uuid, value, datasheet, identifiers, AKAs) and its OpenPnP fields in
// OpenPnP's shape. What a board keeps as its copy of a part it uses (DESIGN.md,
// Board part), what the library's diagnostics show; and the copy's
// fingerprint, which says whether the library's part is still the same.
class JPLibraryJson {
public:
    static JJson     part(const JPPart& p);
    static JPPart    part(const JJson& j);
    static JJson     package(const JPPackage& k);
    static JPPackage package(const JJson& j);
    // A part (with its package, when it has one) reduced to a short text that changes when anything a job
    // places it by changes (its packagings too: how it comes is how it is picked): not its names (AKAs,
    // identifiers are learned, not placed by), its offers, nor its uuid.
    static std::string fingerprint(const JPPart& p, const JPPackage* k);
    static JJson       packaging(const JPPart::Packaging& k);
};

} // inline namespace jf
