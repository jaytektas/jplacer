// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <string>
#include <vector>

inline namespace jf {

// What a column of a CAD file (a CPL, a BOM, any table) holds, in jplacer's
// terms (DESIGN.md, Import): the placement's (designator, position,
// rotation, side, do-not-place) or the part's (value, footprint, MPN, …);
// kept as an extra under its own name; or ignored. Guessed from the column's
// header by the names CAD tools and suppliers give it.
class JPImportField {
public:
    enum class Id {
        Ignore, Extra,
        Designator, X, Y, Rotation, Side, DoNotPlace,
        Value, Footprint, Package, Description, Manufacturer, Mpn, Supplier, SupplierPn, Height, Datasheet, Quantity,
    };

    // In the order a column's choice offers them.
    static const std::vector<Id>& all();
    // What the choice shows ("Supplier PN", "Keep as extra").
    static const char* label(Id id);
    // Its name in files ("supplierPn"); fromKey reads it back (Ignore when unknown).
    static const char* key(Id id);
    static Id          fromKey(const std::string& key);
    // A placement's field (from the placement file), not a part's.
    static bool isPlacement(Id id);
    // The field a header names, by the names tools use for it ("Ref-X(mm)", "Mid X", "PosX" are X;
    // "Mfr Part #" is the MPN); Extra when it names none of them.
    static Id guess(const std::string& header);
    // A header made comparable: upper case, letters and digits only, a units suffix (mm, mil, in) taken
    // off ("Ref-X(mm)" is "REFX").
    static std::string normalise(const std::string& header);
};

} // inline namespace jf
