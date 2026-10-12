// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// The Placements' Part ▸ and Package ▸ columns: each placement's part's (the Parts table's, its fields with
// it) and its package's, after the placements' own and hidden until chosen; shown and edited as the Parts
// table does it, the library's part changed for every placement of it; a placement with no part shows
// nothing there and changes nothing; their edits are the library's to undo, not the table's.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "ui/JPFieldsModel.h"
#include "ui/JPLinkedColumnsModel.h"
#include "ui/JPPackagesTableModel.h"
#include "ui/JPPartsTableModel.h"
#include "ui/JPPlacementsTableModel.h"

#include <filesystem>

using namespace jf;
namespace fs = std::filesystem;

namespace {

int columnKeyed(const JPTableModel& m, const std::string& key) {
    for (int c = 0; c < m.columnCount(); ++c)
        if (JPTableModel::keyOf(m.column(c)) == key) return c;
    return -1;
}

} // namespace

int main() {
    const fs::path dir = fs::temp_directory_path() / "jplacer-test-linked-columns";
    fs::remove_all(dir);
    fs::create_directories(dir);
    JPConfiguration config(dir.string());
    auto package = std::make_shared<JPPackage>();
    package->id = "pkg-0603";
    package->label = "R0603";
    config.addPackage(package);
    auto part = std::make_shared<JPPart>();
    part->id = "part-10k";
    part->value = "10k";
    part->packageId = package->id;
    config.addPart(part);
    std::string error;
    auto board = config.board((dir / "b.jpboard").string(), error);
    assert(board);
    for (const char* id : { "R1", "R2", "R3" }) {
        JPPlacement p;
        p.id = id;
        if (std::string(id) != "R3") p.partId = part->id;
        board->placements.push_back(p);
    }

    JPPlacementsTableModel placements(config, [] { return static_cast<const JPJob*>(nullptr); },
                                      { JPPlacementsTableModel::kEnabled, JPPlacementsTableModel::kId });
    placements.setHolder(board.get());
    JPPartsTableModel parts(config);
    JPFieldsModel partFields(parts, config.fields(), JPFieldSet::Of::Parts);
    JPPackagesTableModel packages(config);
    JPFieldsModel packageFields(packages, config.fields(), JPFieldSet::Of::Packages);
    auto partRow = [&](int row) {
        const JPPlacement* p = placements.placement(row);
        const JPPart* pt = p ? config.part(p->partId) : nullptr;
        return pt ? parts.rowOf(pt) : -1;
    };
    auto packageRow = [&](int row) {
        const JPPlacement* p = placements.placement(row);
        const JPPart* pt = p ? config.part(p->partId) : nullptr;
        const JPPackage* k = pt ? config.package(pt->packageId) : nullptr;
        return k ? packages.rowOf(k) : -1;
    };
    std::vector<std::set<int>> edited;
    JPLinkedColumnsModel linked(placements,
                                { { "Part", "part", &partFields, { "source", "id" }, partRow,
                                    [&](const std::set<int>& rows) { edited.push_back(rows); } },
                                  { "Package", "package", &packageFields, { "source", "id" }, packageRow, nullptr } });
    int libraryChanged = 0;
    parts.onChanged = [&] { ++libraryChanged; };

    // The placements' own first, then the part's (not its GUID nor its icon), then the package's.
    assert(columnKeyed(linked, "id") == 1);
    assert(columnKeyed(linked, "part.source") < 0 && columnKeyed(linked, "part.id") < 0);
    const int value = columnKeyed(linked, "part.value");
    const int packageName = columnKeyed(linked, "package.name");
    assert(value >= placements.columnCount() && packageName > value);
    assert(linked.column(value).name == "Part \xE2\x96\xB8 Value" && linked.column(value).hiddenAtFirst);
    assert(linked.fieldsOf() == JPFieldSet::Of::Placements);

    const int r1 = placements.rowOf("R1"), r2 = placements.rowOf("R2"), r3 = placements.rowOf("R3");
    assert(linked.text(r1, value) == "10k" && linked.text(r1, packageName) == "R0603");
    // No part: nothing shown, nothing changed.
    assert(linked.text(r3, value).empty() && !linked.editable(r3, value));
    assert(!linked.setText(r3, value, "1k", error) && !error.empty());

    // Changed on R1: the library's part, so R2 (the same part) shows it too.
    assert(linked.editable(r1, value));
    assert(linked.setText(r1, value, "4k7", error));
    assert(part->value == "4k7" && linked.text(r2, value) == "4k7");
    assert(libraryChanged == 1 && edited.size() == 1 && *edited[0].begin() == parts.rowOf(part.get()));

    // Many rows at once (a paste): the part told once, at the end.
    linked.beginEdits();
    assert(linked.setText(r1, value, "1k", error) && linked.setText(r2, value, "2k2", error));
    assert(edited.size() == 1);
    linked.endEdits();
    assert(edited.size() == 2 && edited[1].size() == 1 && part->value == "2k2");

    // The library's history undoes the part's columns; the table's, the placements' own.
    assert(linked.ownHistoryAt(value) && !linked.ownHistoryAt(columnKeyed(linked, "id")));

    // A field added to parts: a column of its own here too, its value the part's.
    const int before = linked.columnCount();
    JPField tolerance;
    tolerance.name = "Tolerance";
    const std::string key = config.fields().of(JPFieldSet::Of::Parts).add(tolerance);
    config.fields().changed();
    assert(linked.columnCount() == before + 1);
    const int tol = columnKeyed(linked, "part." + key);
    assert(tol >= 0 && linked.column(tol).name == "Part \xE2\x96\xB8 Tolerance");
    assert(linked.setText(r1, tol, "1%", error) && linked.text(r2, tol) == "1%");

    // Its supplier part number (where an LCSC C number is kept): shown, typed (an LCSC one's supplier LCSC),
    // changed, emptied.
    const int spn = columnKeyed(linked, "part.supplierPn");
    assert(spn >= 0 && linked.editable(r1, spn) && linked.text(r1, spn).empty());
    assert(linked.setText(r1, spn, " c14663 ", error) && linked.text(r2, spn) == "C14663");
    assert(part->identifiers.size() == 1 && part->identifiers[0].kind == "supplierPn" && part->identifiers[0].org == "LCSC");
    assert(linked.setText(r1, spn, "C25804", error) && part->identifiers.size() == 1 && part->identifiers[0].code == "C25804");
    assert(linked.setText(r1, spn, "", error) && part->identifiers.empty());

    fs::remove_all(dir);
    return 0;
}
