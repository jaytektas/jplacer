// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPChooserTable.h"

#include "JPTableTools.h"

#include "JPIconButton.h"
#include "JPPackagesTableModel.h"
#include "JPPartsTableModel.h"

#include <j/core/JStyle.h>
#include <j/core/MenuSystem.h>

#include <map>

inline namespace jf {

// The rows the dropdown shows: each an entry of the choice, the source's row of its key (an entry the source
// has no row for, none or a value not in the library, shows its key alone, under the ID). Read only.
class JPChooserTable::Rows : public JPTableModel {
public:
    Rows(const JPTableModel& source, std::vector<std::string> keys) : m_source(source), m_keys(std::move(keys)) {
        for (int r = 0; r < source.rowCount(); ++r) m_rowOf.emplace(source.rowKey(r), r);
        for (int c = 0; c < source.columnCount(); ++c)
            if (source.column(c).name == "ID") m_idColumn = c;
    }
    void setEntries(std::vector<int> entries) { m_entries = std::move(entries); }
    int  entry(int row) const { return row >= 0 && size_t(row) < m_entries.size() ? m_entries[size_t(row)] : -1; }
    int  rowOfEntry(int entry) const {
        for (size_t r = 0; r < m_entries.size(); ++r)
            if (m_entries[r] == entry) return int(r);
        return -1;
    }

    int    columnCount() const override { return m_source.columnCount(); }
    Column column(int c) const override { return m_source.column(c); }
    int    rowCount() const override { return int(m_entries.size()); }
    std::string text(int row, int c) const override {
        const int s = source(row);
        if (s >= 0) return m_source.text(s, c);
        return c == m_idColumn ? label(row) : std::string();
    }
    double number(int row, int c) const override { const int s = source(row); return s >= 0 ? m_source.number(s, c) : 0; }
    bool   checked(int row, int c) const override { const int s = source(row); return s >= 0 && m_source.checked(s, c); }
    std::string rowKey(int row) const override { return key(row); }
    std::optional<int> compare(int a, int b, int c) const override {
        const int sa = source(a), sb = source(b);
        return sa >= 0 && sb >= 0 ? m_source.compare(sa, sb, c) : std::nullopt;
    }
    std::string cellTooltip(int row, int c) const override { const int s = source(row); return s >= 0 ? m_source.cellTooltip(s, c) : ""; }
    std::string displayText(int row, int c) const override {
        const int s = source(row);
        if (s >= 0) return m_source.displayText(s, c);
        return text(row, c);
    }
    std::string    cellIcon(int row, int c) const override { const int s = source(row); return s >= 0 ? m_source.cellIcon(s, c) : ""; }
    const uint8_t* cellTint(int row, int c) const override { const int s = source(row); return s >= 0 ? m_source.cellTint(s, c) : nullptr; }
    const uint8_t* cellInk(int row, int c) const override { const int s = source(row); return s >= 0 ? m_source.cellInk(s, c) : nullptr; }
    bool cellDimmed(int row, int c) const override { const int s = source(row); return s >= 0 && m_source.cellDimmed(s, c); }
    // The source's fields (its table's columns, those added among them), read only.
    std::optional<JPFieldSet::Of> fieldsOf() const override { return m_source.fieldsOf(); }
    const JPFieldValues* custom(int row) const override { const int s = source(row); return s >= 0 ? m_source.custom(s) : nullptr; }
    bool customEditable(int) const override { return false; }

private:
    std::string key(int row) const {
        const int e = entry(row);
        return e >= 0 && size_t(e) < m_keys.size() ? m_keys[size_t(e)] : std::string();
    }
    std::string label(int row) const {
        const std::string k = key(row);
        return k.empty() ? "(none)" : k;
    }
    int source(int row) const {
        const std::string k = key(row);
        if (k.empty()) return -1;
        const auto at = m_rowOf.find(k);
        return at == m_rowOf.end() ? -1 : at->second;
    }

    const JPTableModel&        m_source;
    std::vector<std::string>   m_keys;
    std::map<std::string, int> m_rowOf;
    std::vector<int>           m_entries;
    int                        m_idColumn = 0;
};

JPChooserTable::Maker JPChooserTable::parts(JPConfiguration& config, std::vector<std::string> keys) {
    return [&config, keys = std::move(keys)](JSceneGraph& g) -> std::unique_ptr<JComboBox::Search::Table> {
        return std::make_unique<JPChooserTable>(g, std::make_unique<JPPartsTableModel>(config), keys, kParts);
    };
}

JPChooserTable::Maker JPChooserTable::partsNamed(JPConfiguration& config, const std::vector<std::string>& names) {
    std::vector<std::string> keys;
    for (const std::string& n : names) {
        const JPPart* p = n.empty() ? nullptr : config.partNamed(n);
        keys.push_back(p ? p->id : n);
    }
    return parts(config, std::move(keys));
}

JPChooserTable::Maker JPChooserTable::packages(JPConfiguration& config, std::vector<std::string> keys) {
    return [&config, keys = std::move(keys)](JSceneGraph& g) -> std::unique_ptr<JComboBox::Search::Table> {
        return std::make_unique<JPChooserTable>(g, std::make_unique<JPPackagesTableModel>(config), keys, kPackages);
    };
}

JPChooserTable::JPChooserTable(JSceneGraph& graph, std::unique_ptr<JPTableModel> source, std::vector<std::string> keys,
                               const std::string& stateKey)
    : m_graph(graph)
    , m_source(std::move(source))
    , m_rows(std::make_unique<Rows>(*m_source, std::move(keys))) {
    m_owned = std::make_unique<JPTable>(graph);
    m_table = m_owned.get();
    m_table->setModel(m_rows.get());
    m_table->setStateKey(stateKey);
    // A click takes the row (a choice is made in one click, as a combo's list's); the keys only walk.
    m_table->onSelectionChanged.connect([this] {
        if (m_quiet || !onPicked) return;
        if (const int e = m_rows->entry(m_table->selectedRow()); e >= 0) onPicked(e);
    });
}

JPChooserTable::~JPChooserTable() = default;

std::unique_ptr<JWidget> JPChooserTable::takeWidget() { return std::move(m_owned); }

float JPChooserTable::width() const {
    // As wide as its columns shown would be at the style's default width each.
    size_t shown = 0;
    for (const JPTable::ColumnShown& c : m_table->columnsShown())
        if (c.shown) ++shown;
    return float(shown) * JStyle::current().gridDefaultColumnWidth + JStyle::current().scrollBarWidth;
}

void JPChooserTable::show(const std::vector<int>& entries, int current) {
    m_quiet = true;
    m_rows->setEntries(entries);
    m_table->refresh();
    if (const int r = m_rows->rowOfEntry(current); r >= 0) m_table->selectRow(r);
    else m_table->clearSelection();
    m_quiet = false;
}

int JPChooserTable::chosen() const { return m_rows->entry(m_table->selectedRow()); }

bool JPChooserTable::key(const JKeyEvent& ke) {
    m_quiet = true;
    const bool taken = m_table->handleKeyEvent(ke);
    m_quiet = false;
    return taken;
}

std::unique_ptr<JWidget> JPChooserTable::barControl(JSceneGraph& graph) {
    // Its tools (Columns…, its views' buttons): menus opened on the screen where the dropdown is.
    auto tools = std::make_unique<JPTableTools>(graph, [&graph](JMenu* menu, float x, float y) {
        if (!JMenuManager::instance().onOpenMenu) return;
        const auto& host = graph.hostWindow();
        JMenuManager::instance().onOpenMenu(menu, host.screenX + int(x), host.screenY + int(y), false, false);
    });
    tools->attach(*m_table);
    // A read-only list: its views, but no fields to add or edit from here.
    m_table->setOffersFields(false);
    return tools;
}

} // inline namespace jf
