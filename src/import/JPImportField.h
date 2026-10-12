// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "model/JPFields.h"

#include <string>
#include <vector>

inline namespace jf {

// What a column of a CAD file (a CPL, a BOM, any table) holds (DESIGN.md, Import): a field of one of the
// kinds an import fills (Target): the placement's, the part's, the package's, the board's or the job's,
// jplacer's own or one added (JPFields); or nothing (ignored). Guessed from the column's header by the
// headings each field is matched by (JPField::matchesHeading). The import reads jplacer's own fields itself
// (Id: a designator, a position, a value…); one added it keeps as its kind's value.
class JPImportField {
public:
    // jplacer's own fields an import reads, each the built-in of its kind its key names (target).
    enum class Id {
        Ignore,
        Designator, X, Y, Rotation, Side, DoNotPlace,
        Name, Value, Footprint, Package, Description, Manufacturer, Mpn, Supplier, SupplierPn, Height, Datasheet, Quantity,
    };
    // A field a column fills: its kind and its key; no key: nothing.
    struct Target {
        JPFieldSet::Of of = JPFieldSet::Of::Parts;
        std::string    key;
        bool empty() const { return key.empty(); }
        bool operator==(const Target& o) const { return key == o.key && (key.empty() || of == o.of); }
        // In files: "placements:x", "parts:u3"; empty: "".
        std::string        text() const;
        static Target      fromText(const std::string& text);
    };

    // The kinds an import fills, in the order a column's choice offers them.
    static const std::vector<JPFieldSet::Of>& kinds();
    // Whether a kind's field takes one value for the whole import (the board's, the job's), not a row's.
    static bool single(JPFieldSet::Of of) { return of == JPFieldSet::Of::Boards || of == JPFieldSet::Of::Job; }
    // Every field there is that an import may fill, of those kinds, in order.
    static std::vector<Target> offered(const JPFields& fields);
    // What a column's choice shows: "Placement ▸ ID", "Part ▸ LCSC"; nothing: "Ignore".
    static std::string label(const JPFields& fields, const Target& t);
    // The field a header names, by the headings each field is matched by (none taken by an earlier column, in
    // `taken`): the fields added first (one made for a heading takes it), then jplacer's; each placements' first,
    // then parts', packages', boards' and the job's; none: nothing.
    static Target guess(const JPFields& fields, const std::string& header, const std::vector<Target>& taken);

    // jplacer's own field an import reads, as a target; a target as one (Ignore: an added field, or nothing).
    static Target target(Id id);
    static Id     idOf(const Target& t);
    // Its name, as the import says it ("Designator", "Supplier PN").
    static const char* name(Id id);
    // The built-in's own key an Id is (its JPField::importAs, "supplierPn").
    static const char* key(Id id);
    // A placement's field (from the placement file), not a part's.
    static bool isPlacement(Id id);
    // Whether a header is one jplacer's own fields are known by (the names CAD tools give them).
    static bool known(const std::string& header);
    // A header made comparable: upper case, letters and digits only, a units suffix (mm, mil, in) taken
    // off ("Ref-X(mm)" is "REFX").
    static std::string normalise(const std::string& header);
};

} // inline namespace jf
