// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPImportField.h"
#include "JPTableFile.h"

#include "model/JPFootprint.h"
#include "model/JPLengthUnit.h"

#include <j/config/Json.h>

#include <map>
#include <string>
#include <vector>

inline namespace jf {

// One file a board is imported from (DESIGN.md, Import): what it is (the
// placement file, a BOM, another table), its table, what each of its
// columns holds (JPImportField), the units its lengths are in where a cell
// does not say, and the mapping profile it came from; a board file (a KiCad
// .kicad_pcb, JPKicadBoardFile) also its footprints. Kept with the board
// as it was read (provenance), so its mapping can be changed later and the
// board matched again without importing again.
class JPImportSource {
public:
    enum class Role { Cpl, Bom, Other };

    Role                           role = Role::Cpl;
    JPTableFile                    table;
    std::vector<JPImportField::Id> mapping;   // one per header column
    JPLengthUnit                   units = JPLengthUnit::Millimeters;
    std::string                    profile;   // the mapping profile used, if one was
    // A board file's footprints, by the name its Footprint column gives them, and what reading it found
    // worth saying (a name drawn two ways).
    std::map<std::string, JPFootprint> footprints;
    std::vector<std::string>           notes;

    static const char* roleName(Role r);   // "cpl", "bom", "table"
    static const char* roleLabel(Role r);  // "Placement file (CPL)", "BOM", "Other table"

    // Each column's field guessed from its header (a field taken by an earlier column: kept as an extra), a
    // "Package" column the footprint where none is named so (KiCad's .pos, many CPLs), and the units from the
    // headers ("Ref-X(mil)") or the cells ("12.5mm").
    void guess();
    // The column mapped to `id`, else -1; a row's cell there, else empty.
    int                column(JPImportField::Id id) const;
    const std::string& cell(const std::vector<std::string>& row, JPImportField::Id id) const;
    // A length as a cell gives it ("12.5", "12,5", "12.5mm", "492mil", "0.5in"), in millimetres; `units` when
    // it does not say. False when it is not a number.
    static bool length(const std::string& cell, JPLengthUnit units, double& mm);

    // As kept with the board: role, file, when, profile, units, the mapping by header, every row as read.
    JJson provenance(const std::string& when) const;
};

} // inline namespace jf
