// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "job/JPPlacement.h"

#include <functional>
#include <string>
#include <vector>

inline namespace jf {

// A CSV table from a PCB tool (a pick-and-place file, a BOM): its heading row
// found and each column told by its heading, whatever the tool calls it
// (Designator / Ref, Mid X / PosX, Supplier Part / LCSC Part #, Manufacturer
// Part / MPN, ...), comma, semicolon or tab between fields, quoted fields, a
// byte-order mark; then its records. The importers share it, so a column is
// read the same way from every file.
class JPCsvTable {
public:
    // What each column is, from its heading: the names PCB tools use.
    enum class Column {
        Designator, X, Y, Rotation, Side, Footprint, Value,
        PadX, PadY, Supplier, SupplierNumber, Mpn, Manufacturer, Tolerance, Voltage, Power, Dielectric,
        Temperature, Smd, Mounting, Fitted, DoNotPlace, SupplierPackage, Pins, Description, Device, Datasheet,
        CadId, Other, Count
    };
    struct Heading {
        Column      column = Column::Other;
        double      unitsPerMm = 0;   // from "(mm)", "(mil)" in the heading; 0: not said
        int         rank = 0;         // among several for one column, the higher is used
        std::string supplier;         // a supplier number column's supplier, where its heading names it ("LCSC Part")
    };
    using Record = std::vector<std::string>;

    // The table in `text`: its heading row is the first that `isHeading`
    // accepts. False, with why, when there is none.
    bool parse(const std::string& text, const std::function<bool(const JPCsvTable&)>& isHeading, std::string& error);

    bool               has(Column c) const { return m_col[int(c)] >= 0; }
    const Heading&     heading(Column c) const { return m_heads[size_t(m_col[int(c)])]; }
    std::string        field(const Record& r, Column c) const;
    const std::vector<Record>& records() const { return m_records; }
    // The line each record was on (1 is the first), for notes.
    const std::vector<int>& lines() const { return m_lines; }

    // What a record says about the part (supplier numbers, MPN, ratings,
    // mounting and do-not-place, package names and pins, what is only shown,
    // and every column not known, as text), onto `p`.
    void partFields(const Record& r, JPPlacement& p) const;

    static std::string trim(const std::string& s);
    static std::string lower(std::string s);
    // A length: a number with an optional unit after it ("12.5mm", "500 mil").
    static bool length(const std::string& text, double headingUnitsPerMm, double& mm);
    static bool sideOf(const std::string& text, JPPlacement::Side& side);
    static bool isYes(const std::string& text);
    static bool isNo(const std::string& text);

private:
    std::vector<std::string> m_names;
    std::vector<Heading>     m_heads;
    std::vector<Record>      m_records;
    std::vector<int>         m_lines;
    int                      m_col[int(Column::Count)] = {};
    std::vector<size_t>      m_numberCols;   // every supplier number column (a file may give several suppliers)
};

} // inline namespace jf
