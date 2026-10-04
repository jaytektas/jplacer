// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPEntry.h"
#include "JPPartsStore.h"

#include <string>
#include <vector>

inline namespace jf {

// What can be edited of a part, a package or a footprint, each field as
// text, so one form shows and edits any of them: lists as "a, b, c",
// supplier numbers as "LCSC C17408; Digi-Key 311-100CRCT-ND", lengths in
// millimetres. An edit that changes something raises the entry's revision.
class JPEntryFields {
public:
    struct Field {
        std::string key;
        std::string label;
    };

    static const std::vector<Field>& of(JPEntry::Kind kind);
    // The field's text; empty when the entry is not there.
    static std::string get(const JPPartsStore& store, const JPEntry& e, const std::string& key);
    // False, with why, when the text does not make a value (a length that is
    // not a number, a name another package has); nothing is changed then.
    static bool set(JPPartsStore& store, const JPEntry& e, const std::string& key, const std::string& text,
                    std::string& error);
    // The fields whose text differs between two entries of one kind (in two
    // stores): "Height: 0.45 → 0.6".
    static std::vector<std::string> differences(const JPPartsStore& a, const JPEntry& ea, const JPPartsStore& b,
                                                const JPEntry& eb);
};

} // inline namespace jf
