// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <string>

inline namespace jf {

// One part, package or footprint in a parts store, by kind and id.
struct JPEntry {
    enum class Kind { Part, Package, Footprint };

    Kind        kind = Kind::Part;
    std::string id;

    bool operator==(const JPEntry&) const = default;
    static const char* name(Kind k) {
        switch (k) {
            case Kind::Part:      return "part";
            case Kind::Package:   return "package";
            case Kind::Footprint: return "footprint";
        }
        return "";
    }
};

} // inline namespace jf
