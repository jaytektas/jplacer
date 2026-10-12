// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// A table's views (JPTable): Standard and All Fields to begin with (a layout kept from before views Standard's);
// All Fields shows every column, those hidden until chosen too, and cannot hide one; a new view made from the
// one shown, every column of All Fields' among it, with an icon of its own; each view keeps its own layout as it
// is changed; one locked shows a change until another is shown or it is reverted; one may keep its search (shown
// with it); views renamed, moved, duplicated and deleted (the last one stays); all of it kept for the next table
// with the key.
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

// A, B, and C (hidden until chosen).
struct Three : JPTableModel {
    int    columnCount() const override { return 3; }
    Column column(int c) const override {
        Column col { std::string(1, char('A' + c)), "", Kind::Text };
        col.hiddenAtFirst = c == 2;
        return col;
    }
    int    rowCount() const override { return 2; }
    std::string text(int r, int c) const override { return std::string(1, char('a' + c)) + std::to_string(r); }
};

std::vector<std::string> shown(const JPTable& t) {
    std::vector<std::string> out;
    for (const JPTable::ColumnShown& c : t.columnsShown())
        if (c.shown) out.push_back(c.name);
    return out;
}

using Names = std::vector<std::string>;

} // namespace

int main() {
    std::map<std::string, JJson> kept;
    JPTable::s_loadState = [&kept](const std::string& key) { return kept.count(key) ? kept[key] : JJson(); };
    JPTable::s_saveState = [&kept](const std::string& key, const JJson& state) { kept[key] = state; };
    JSceneGraph graph;
    Three model;
    {
        // A layout kept before there were views: Standard's.
        JJson old = JJson::object();
        JJson hidden = JJson::array();
        hidden.push(JJson("B"));
        old["hidden"] = hidden;
        JJson order = JJson::array();
        for (const char* k : { "A", "B", "C" }) order.push(JJson(k));
        old["order"] = order;
        kept["t"] = old;
    }
    {
        JPTable t(graph);
        t.setModel(&model);
        t.setStateKey("t");
        assert(t.views().size() == 2 && t.views()[0].name == "Standard" && t.views()[1].all && t.currentView() == 0);
        assert((shown(t) == Names{ "A", "C" }));   // C known to the old layout: shown; B hidden
        // All Fields: every one, and none hidden there.
        t.showView(1);
        assert((shown(t) == Names{ "A", "B", "C" }));
        t.setColumnShown("A", false);
        assert((shown(t) == Names{ "A", "B", "C" }));
        // A new view made from it: every column, a free icon, shown.
        const int mine = t.addView("Mine");
        assert(mine == 2 && t.currentView() == 2 && (shown(t) == Names{ "A", "B", "C" }));
        assert(t.views()[2].icon != t.views()[0].icon && t.views()[2].icon != t.views()[1].icon && !t.views()[2].all);
        // Its own layout: a column hidden here, not in Standard.
        t.setColumnShown("B", false);
        assert((shown(t) == Names{ "A", "C" }));
        t.showView(0);
        assert((shown(t) == Names{ "A", "C" }));
        t.setColumnShown("B", true);
        assert((shown(t) == Names{ "A", "B", "C" }));
        t.showView(2);
        assert((shown(t) == Names{ "A", "C" }));
        // Locked: a change shown, not kept; another view shown and back: as it was.
        t.setViewLocked(2, true);
        t.setColumnShown("C", false);
        assert(t.viewChanged() && (shown(t) == Names{ "A" }));
        t.showView(0);
        t.showView(2);
        assert(!t.viewChanged() && (shown(t) == Names{ "A", "C" }));
        t.setColumnShown("C", false);
        t.revertView();
        assert(!t.viewChanged() && (shown(t) == Names{ "A", "C" }));
        // Unlocked while changed: the change is its own.
        t.setColumnShown("C", false);
        t.setViewLocked(2, false);
        t.showView(0);
        t.showView(2);
        assert((shown(t) == Names{ "A" }));
        t.setColumnShown("C", true);
        // A view keeping its search: shown with it.
        std::string said;
        t.onSearchShown.connect([&said](const std::string& s) { said = s; });
        t.setFilter("a1");
        t.setViewKeepsSearch(2, true);
        t.setFilter("a0");
        t.showView(0);
        t.showView(2);
        assert(said == "a0");
        // Renamed, moved, duplicated, deleted; the last stays.
        t.renameView(2, "Supplier");
        t.moveView(2, 0);
        assert(t.views()[0].name == "Supplier" && t.currentView() == 0 && t.views()[1].name == "Standard");
        const int copy = t.duplicateView(0);
        assert(copy == 1 && t.views()[1].name == "Supplier (copy)" && t.currentView() == 1 && !t.views()[1].locked);
        t.deleteView(1);
        assert(t.views().size() == 3 && t.currentView() == 1 && t.views()[1].name == "Standard");
        t.showView(0);
    }
    {
        // The next table with the key: its views as left, the one shown shown.
        JPTable t(graph);
        t.setModel(&model);
        t.setStateKey("t");
        assert(t.views().size() == 3 && t.views()[0].name == "Supplier" && t.currentView() == 0 && t.views()[0].keepSearch);
        assert((shown(t) == Names{ "A", "C" }));
        while (t.views().size() > 1) t.deleteView(0);
        t.deleteView(0);
        assert(t.views().size() == 1);
    }
    {
        // Show in Every View: out of every view's hidden, the one shown's too.
        JPTable t(graph);
        t.setModel(&model);
        t.setStateKey("every");
        t.setColumnShown("B", false);
        t.addView("Other");
        assert((shown(t) == Names{ "A" }));   // C hidden until chosen, B hidden as made
        t.showInEveryView("C");
        assert((shown(t) == Names{ "A", "C" }));
        t.showView(0);
        assert((shown(t) == Names{ "A", "C" }));
    }
    return 0;
}
