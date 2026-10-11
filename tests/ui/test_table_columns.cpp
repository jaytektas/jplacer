// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// A table's columns can be hidden (a toolbar's Columns…) and shown again in their place; which are hidden is
// kept with the table's order, widths and sort, and brought back by the next table with its key; the last
// one shown stays; Reset Columns shows them all, but for those hidden until chosen.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "ui/JPTable.h"

#include <j/core/SceneGraph.h>

#include <algorithm>
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
    {
        // A column hidden until chosen: hidden in a table that has never had it, shown once chosen (and so
        // kept), hidden again on Reset Columns.
        struct WithMore : Three {
            int    columnCount() const override { return 4; }
            Column column(int c) const override {
                if (c < 3) return Three::column(c);
                Column more { "D", "", Kind::Text };
                more.hiddenAtFirst = true;
                return more;
            }
        } withMore;
        JPTable t(graph);
        t.setModel(&withMore);
        t.setStateKey("more");
        assert((shown(t) == std::vector<std::string>{ "A", "B", "C" }));
        t.setColumnShown("D", true);
        JPTable again(graph);
        again.setModel(&withMore);
        again.setStateKey("more");
        assert((shown(again) == std::vector<std::string>{ "A", "B", "C", "D" }));
        again.resetColumns();
        assert((shown(again) == std::vector<std::string>{ "A", "B", "C" }));
        // A table kept before the column came: the new one comes hidden.
        JPTable older(graph);
        older.setModel(&withMore);
        older.setStateKey("test");
        const std::vector<std::string> olderShown = shown(older);
        assert(std::find(olderShown.begin(), olderShown.end(), std::string("D")) == olderShown.end());
    }
    return 0;
}
