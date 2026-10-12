// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// A table's search (JPTable::setFilter): anywhere in any column, a hidden one's too; "name: text" in the column
// so headed alone (without regard to case), "name:" the rows with anything in it; a colon after what names no
// column is part of the search. What it leaves is counted against what there would be without it. The search
// line completes a column's name (JPSearchField::completion).
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "ui/JPSearchField.h"
#include "ui/JPTable.h"

#include <j/core/SceneGraph.h>

#include <string>
#include <vector>

using namespace jf;

namespace {

// Rows of a designator, a value and a supplier's code (some empty).
struct Parts : JPTableModel {
    std::vector<std::vector<std::string>> rows { { "R1", "10k", "C25804" }, { "R2", "4k7", "" }, { "C1", "100n", "C14663" },
                                                 { "U1", "http://x", "" } };
    int    columnCount() const override { return 3; }
    Column column(int c) const override {
        static const char* names[] = { "Designator", "Value", "LCSC" };
        return { names[c], "", Kind::Text };
    }
    int    rowCount() const override { return int(rows.size()); }
    std::string text(int r, int c) const override { return rows[size_t(r)][size_t(c)]; }
};

} // namespace

int main() {
    JSceneGraph graph;
    Parts model;
    JPTable t(graph);
    t.setModel(&model);
    assert(!t.searching() && t.viewRowCount() == 4 && t.unsearchedRowCount() == 4);
    t.setFilter("k");   // anywhere: R1's 10k, R2's 4k7
    assert(t.searching() && t.viewRowCount() == 2 && t.unsearchedRowCount() == 4);
    t.setFilter("lcsc: 25");   // the LCSC column alone
    assert(t.viewRowCount() == 1 && t.modelRowAt(0) == 0);
    t.setFilter("LCSC:");      // anything in it
    assert(t.viewRowCount() == 2);
    t.setFilter("value: 4K");  // case aside
    assert(t.viewRowCount() == 1 && t.modelRowAt(0) == 1);
    t.setFilter("http://");    // no column is "http": the whole of it searched
    assert(t.viewRowCount() == 1 && t.modelRowAt(0) == 3);
    // A column hidden is still searched.
    t.setColumnShown("LCSC", false);
    t.setFilter("14663");
    assert(t.viewRowCount() == 1 && t.modelRowAt(0) == 2);
    t.setFilter("");
    assert(!t.searching() && t.viewRowCount() == 4);
    // Names completed.
    assert((t.columnNames() == std::vector<std::string>{ "Designator", "Value", "LCSC" }));
    JPSearchField f(graph);
    f.attach(t);
    f.setText("lc");
    assert(f.completion() == "SC: ");
    f.setText("x");
    assert(f.completion().empty());
    f.setText("lcsc: ");
    assert(f.completion().empty() && t.viewRowCount() == 2);   // what is typed is the table's search
    return 0;
}
