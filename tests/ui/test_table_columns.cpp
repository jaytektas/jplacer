// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// A table's columns can be hidden (a toolbar's Columns…) and shown again in their place; which are hidden is
// kept with the table's order, widths and sort, and brought back by the next table with its key; the last
// one shown stays; Reset Columns shows them all.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "ui/JPTable.h"

#include <j/core/SceneGraph.h>

#include <map>
#include <string>
#include <vector>

using namespace jf;

namespace {

struct Three : JPTableModel {
    int    columnCount() const override { return 3; }
    Column column(int c) const override { return { std::string(1, char('A' + c)), "", Kind::Text }; }
    int    rowCount() const override { return 1; }
    std::string text(int, int c) const override { return std::string(1, char('a' + c)); }
};

std::vector<std::string> shown(const JPTable& t) {
    std::vector<std::string> out;
    for (const JPTable::ColumnShown& c : t.columnsShown())
        if (c.shown) out.push_back(c.name);
    return out;
}

} // namespace

int main() {
    std::map<std::string, JJson> kept;
    JPTable::s_loadState = [&kept](const std::string& key) { return kept.count(key) ? kept[key] : JJson(); };
    JPTable::s_saveState = [&kept](const std::string& key, const JJson& state) { kept[key] = state; };

    JSceneGraph graph;
    Three model;
    {
        JPTable t(graph);
        t.setModel(&model);
        t.setStateKey("test");
        assert((shown(t) == std::vector<std::string>{ "A", "B", "C" }));
        t.setColumnShown("B", false);
        assert((shown(t) == std::vector<std::string>{ "A", "C" }));
        assert(t.columnsShown().size() == 3 && t.columnsShown()[1].name == "B" && !t.columnsShown()[1].shown);   // in its place
        assert(kept["test"]["hidden"].isArray() && kept["test"]["hidden"].arr().size() == 1);
    }
    {
        // The next table with its key: B still hidden.
        JPTable t(graph);
        t.setModel(&model);
        t.setStateKey("test");
        assert((shown(t) == std::vector<std::string>{ "A", "C" }));
        t.setColumnShown("A", false);
        t.setColumnShown("C", false);   // the last one shown stays
        assert((shown(t) == std::vector<std::string>{ "C" }));
        t.setColumnShown("B", true);    // back where it was
        assert((shown(t) == std::vector<std::string>{ "B", "C" }));
        t.resetColumns();
        assert((shown(t) == std::vector<std::string>{ "A", "B", "C" }));
    }
    return 0;
}
