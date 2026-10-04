// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "library/JPEntry.h"
#include "library/JPPartsStore.h"
#include "ui/JPFormPage.h"

#include <string>
#include <vector>

inline namespace jf {

// What the Parts dock needs for showing a part, a package or
// a footprint on a form page (JPEntryFields): its fields (a part's package
// and a package's footprint are chosen, not typed), filling them, and making
// a footprint from numbers typed in one line.
class JPlacerEntryForm {
public:
    static std::vector<JPFormPage::Field> fields(JPEntry::Kind kind);
    // The page showing `e` of `store` (all empty when it is not there).
    static void fill(JPFormPage& page, const JPPartsStore& store, const JPEntry& e);

    // The prompt for a footprint's numbers, and the footprint made from them,
    // named `name`: dual, "pins, pitch, pad centres across, pad length, pad
    // width"; quad, "pins a side, pitch, pad centres across, pad length, pad
    // width, exposed pad (0: none)". Millimetres.
    static const char* prompt(bool quad);
    static bool make(bool quad, const std::string& numbers, const std::string& name, JPFootprint& out, std::string& error);
};

} // inline namespace jf
