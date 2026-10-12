// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// Edits in a table (JPTable): a value typed with several rows chosen goes into all of them (refused by one, by
// none), a tick into all of them; each change one step of the table edits' history (JPTableHistory), undone and
// done again by the rows' and columns' keys, the model told once for many rows; a model with a history of its
// own makes no steps. Ctrl+V pastes the clipboard's cells from the chosen cell down and along, one value into
// every row chosen, a cell that cannot be changed passed over, and nothing at all when a value is refused.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "ui/JPTable.h"

#include <j/core/SceneGraph.h>

#include <cstdlib>
#include <string>
#include <vector>

using namespace jf;

namespace {

// Rows of a name (not editable), a number (refusing anything else) and a tick.
struct Rows : JPTableModel {
    struct Row {
        std::string id, rank;
        bool        on = false;
    };
    std::vector<Row> rows { { "R1", "1" }, { "R2", "2" }, { "R3", "3" }, { "R4", "4" } };
    int  said = 0, batching = 0;
    bool own = false;
    int    columnCount() const override { return 3; }
    Column column(int c) const override {
        static const char* names[] = { "ID", "Rank", "On" };
        return { names[c], "", c == 2 ? Kind::Boolean : c == 1 ? Kind::Number : Kind::Text };
    }
    int    rowCount() const override { return int(rows.size()); }
    std::string text(int r, int c) const override { return c == 0 ? rows[size_t(r)].id : c == 1 ? rows[size_t(r)].rank : ""; }
    bool   checked(int r, int c) const override { return c == 2 && rows[size_t(r)].on; }
    std::string rowKey(int r) const override { return rows[size_t(r)].id; }
    bool   editable(int, int c) const override { return c != 0; }
    bool   setText(int r, int c, const std::string& t, std::string& error) override {
        char* end = nullptr;
        std::strtol(t.c_str(), &end, 10);
        if (c != 1 || t.empty() || *end) {
            error = "not a number";
            return false;
        }
        rows[size_t(r)].rank = t;
        changed();
        return true;
    }
    void   setChecked(int r, int c, bool on) override {
        if (c != 2) return;
        rows[size_t(r)].on = on;
        changed();
    }
    bool ownHistory() const override { return own; }
    void beginEdits() override { ++batching; }
    void endEdits() override {
        if (--batching == 0 && pending) {
            pending = false;
            ++said;
        }
    }

private:
    bool pending = false;
    void changed() {
        if (batching) pending = true;
        else ++said;
    }
};

JKeyEvent key(JKeyEvent::JKey k, const char* text = "", bool ctrl = false) {
    JKeyEvent e;
    e.key = k;
    e.ctrl = ctrl;
    for (size_t i = 0; text[i] && i < 7; ++i) e.utf8[i] = text[i];
    return e;
}

} // namespace

int main() {
    JSceneGraph graph;
    Rows model;
    JPTable t(graph);
    t.setModel(&model);
    JPTableHistory history;
    JPTable::s_history = &history;
    std::string status;
    JPTable::s_status = [&status](const std::string& s) { status = s; };

    // Three rows chosen; Right to Rank; "7" typed and Return: all three.
    t.selectRows({ 0, 1, 2 });
    assert(t.handleKeyEvent(key(JKeyEvent::JKey::Right)));
    assert(t.handleKeyEvent(key(JKeyEvent::JKey::_7, "7")));
    t.handleKeyEvent(key(JKeyEvent::JKey::Return));
    assert(model.rows[0].rank == "7" && model.rows[1].rank == "7" && model.rows[2].rank == "7" && model.rows[3].rank == "4");
    assert(model.said == 1 && history.canUndo() && history.undoText() == "Change Rank of 3 Rows" && !status.empty());
    // Undone, done again.
    history.undo();
    assert(model.rows[0].rank == "1" && model.rows[1].rank == "2" && model.rows[2].rank == "3");
    history.redo();
    assert(model.rows[0].rank == "7" && model.rows[2].rank == "7");
    // Refused by one (a word): by none, the cell left open.
    t.selectRows({ 0, 1 });
    t.handleKeyEvent(key(JKeyEvent::JKey::X, "x"));
    t.handleKeyEvent(key(JKeyEvent::JKey::Return));
    assert(model.rows[0].rank == "7" && model.rows[1].rank == "7");
    t.handleKeyEvent(key(JKeyEvent::JKey::Escape));
    // A tick (Space) in all the rows chosen.
    t.selectRows({ 1, 3 });
    t.handleKeyEvent(key(JKeyEvent::JKey::Right));
    t.handleKeyEvent(key(JKeyEvent::JKey::Space, " "));
    assert(!model.rows[0].on && model.rows[1].on && !model.rows[2].on && model.rows[3].on);
    history.undo();
    assert(!model.rows[1].on && !model.rows[3].on);

    // Paste: a block from the chosen cell, down and along (the ID column, not to be changed, passed over).
    std::string clip;
    JWidget::s_clipboardGet = [&clip] { return clip; };
    t.selectRow(1);
    t.handleKeyEvent(key(JKeyEvent::JKey::Left));   // Rank
    clip = "20\ttrue\n30\tfalse\n";
    t.paste();
    assert(model.rows[1].rank == "20" && model.rows[1].on && model.rows[2].rank == "30" && !model.rows[2].on);
    assert(history.undoText() == "Change 3 Cells of 2 Rows");   // R3 was not on: two of its cells, one changed
    history.undo();
    assert(model.rows[1].rank == "7" && model.rows[2].rank == "7" && !model.rows[1].on);
    // One value into every row chosen.
    t.selectRows({ 0, 1, 2, 3 });
    clip = "5";
    t.paste();
    for (const auto& r : model.rows) assert(r.rank == "5");
    // A value refused: nothing pasted.
    t.selectRow(0);
    clip = "6\nsix\n";
    t.paste();
    assert(model.rows[0].rank == "5" && model.rows[1].rank == "5" && status.find("Nothing pasted") == 0);

    // A model with a history of its own: no steps here.
    model.own = true;
    const long long before = history.serial();
    t.selectRows({ 0, 1 });
    t.handleKeyEvent(key(JKeyEvent::JKey::_9, "9"));
    t.handleKeyEvent(key(JKeyEvent::JKey::Return));
    assert(model.rows[0].rank == "9" && model.rows[1].rank == "9" && history.serial() == before);
    JPTable::s_history = nullptr;
    JPTable::s_status = nullptr;
    JWidget::s_clipboardGet = nullptr;
    return 0;
}
