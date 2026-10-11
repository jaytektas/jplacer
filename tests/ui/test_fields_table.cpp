// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// A table of things with fields (JPFieldsModel under JPTable): a column for each field added, read and edited as
// its type says (a value it cannot read refused); a built-in removed is in no column, one renamed shows its
// name; the table's kept layout follows a column by its key, not its heading. A field added from a table's
// Columns… is shown there, after the column asked for, and hidden in another table of its kind until chosen.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "ui/JPTable.h"

#include <j/core/SceneGraph.h>

#include <map>
#include <string>
#include <vector>

using namespace jf;
using O = JPFieldSet::Of;

namespace {

// Two feeders: Name and Enabled of JPFieldSet's feeders, and their values of the fields added.
struct Feeders : JPTableModel {
    std::vector<JPFieldValues> values = std::vector<JPFieldValues>(2);
    int said = 0;
    int    columnCount() const override { return 2; }
    Column column(int c) const override {
        Column col { c == 0 ? "Name" : "Enabled", "", c == 0 ? Kind::Text : Kind::Boolean };
        col.key = c == 0 ? "name" : "enabled";
        return col;
    }
    int    rowCount() const override { return 2; }
    std::string text(int r, int c) const override { return c == 0 ? "F" + std::to_string(r) : ""; }
    std::optional<JPFieldSet::Of> fieldsOf() const override { return O::Feeders; }
    const JPFieldValues* custom(int r) const override { return &values[size_t(r)]; }
    bool setCustom(int r, const std::string& key, const JJson& v) override {
        values[size_t(r)].set(key, v);
        ++said;
        return true;
    }
};

std::vector<std::string> shown(const JPTable& t) {
    std::vector<std::string> out;
    for (const JPTable::ColumnShown& c : t.columnsShown())
        if (c.shown) out.push_back(c.name);
    return out;
}

void trigger(JMenu* menu, const std::string& label) {
    for (const auto& w : menu->items())
        if (auto* item = dynamic_cast<JMenuItem*>(w.get()); item && item->label() == label) {
            item->onTriggered.emit();
            return;
        }
    assert(!"no such entry");
}

} // namespace

int main() {
    std::map<std::string, JJson> kept;
    JPTable::s_loadState = [&kept](const std::string& key) { return kept.count(key) ? kept[key] : JJson(); };
    JPTable::s_saveState = [&kept](const std::string& key, const JJson& state) { kept[key] = state; };
    JPFields fields;
    JPTable::s_fields = &fields;

    // The model alone: its columns, a field's values.
    Feeders model;
    JPFieldsModel m(model, fields, O::Feeders);
    assert(m.columnCount() == 2);
    JPField reel;
    reel.name = "Reel";
    reel.type = JPField::Type::Number;
    const std::string key = fields.of(O::Feeders).add(reel);
    fields.changed();
    assert(m.columnCount() == 3 && m.column(2).name == "Reel" && m.column(2).key == key && m.column(2).kind == JPTableModel::Kind::Number);
    std::string error;
    assert(m.editable(0, 2) && m.setText(0, 2, "7", error) && model.values[0].get(key).number() == 7 && m.text(0, 2) == "7");
    assert(!m.setText(1, 2, "seven", error) && !error.empty() && !model.values[1].has(key));
    assert(m.setText(0, 2, "", error) && !model.values[0].has(key));   // emptied: no value
    // A built-in renamed, another removed; one jplacer needs stays.
    JPField name = *fields.of(O::Feeders).field("name");
    name.name = "Label";
    assert(fields.of(O::Feeders).update(name, error) && !fields.of(O::Feeders).remove("enabled") );
    fields.changed();
    assert(m.column(0).name == "Label" && m.column(0).key == "name");
    // A Yes/No field: a tick; a Choice: its list, then none.
    JPField spare;
    spare.name = "Spare";
    spare.type = JPField::Type::YesNo;
    const std::string spareKey = fields.of(O::Feeders).add(spare);
    JPField size;
    size.name = "Size";
    size.type = JPField::Type::Choice;
    size.choices = { "7in", "13in" };
    const std::string sizeKey = fields.of(O::Feeders).add(size);
    fields.changed();
    assert(m.columnCount() == 5 && m.column(3).kind == JPTableModel::Kind::Boolean && m.column(4).kind == JPTableModel::Kind::Choice);
    m.setChecked(1, 3, true);
    assert(model.values[1].get(spareKey).boolean() && m.checked(1, 3));
    assert((m.choices(0, 4) == std::vector<std::string>{ "7in", "13in", "" }));
    m.setChoice(0, 4, 1);
    assert(model.values[0].get(sizeKey).str() == "13in");
    m.setChoice(0, 4, 2);
    assert(!model.values[0].has(sizeKey));
    assert(fields.of(O::Feeders).remove(spareKey) && fields.of(O::Feeders).remove(sizeKey));
    fields.changed();

    JSceneGraph graph;
    // A field added from one table: shown there after the column asked for; another table of feeders has it
    // hidden until chosen.
    JPTable::s_fieldHooks.add = [&fields](O of, std::function<void(const std::string&)> added) {
        JPField f;
        f.name = "Lane";
        const std::string k = fields.of(of).add(f);
        fields.changed();
        if (added) added(k);
    };
    {
        JPTable here(graph), there(graph);
        here.setModel(&model);
        here.setStateKey("here");
        there.setModel(&model);
        there.setStateKey("there");
        // Reel, added with no table to show it: hidden until chosen.
        assert((shown(here) == std::vector<std::string>{ "Label", "Enabled" }));
        trigger(here.columnsMenu(), "Add Field…");
        assert((shown(here) == std::vector<std::string>{ "Label", "Enabled", "Lane" }));
        assert((shown(there) == std::vector<std::string>{ "Label", "Enabled" }));
        // Renamed, it keeps its place: the layout is kept by its key.
        JPField lane = *fields.of(O::Feeders).named("Lane");
        lane.name = "Track";
        assert(fields.of(O::Feeders).update(lane, error));
        fields.changed();
        JPTable::fieldsChanged();
        assert((shown(here) == std::vector<std::string>{ "Label", "Enabled", "Track" }));
    }
    {
        // The next table with the key: as it was left.
        JPTable again(graph);
        again.setModel(&model);
        again.setStateKey("here");
        assert((shown(again) == std::vector<std::string>{ "Label", "Enabled", "Track" }));
    }
    JPTable::s_fieldHooks = {};
    JPTable::s_fields = nullptr;
    return 0;
}
