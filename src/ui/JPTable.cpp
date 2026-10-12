// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPTable.h"

#include "JPOpenPnpIcons.h"
#include "JPViewIcons.h"

#include <j/core/Dialog.h>
#include <j/core/JArrow.h>
#include <j/core/JStyle.h>
#include <j/core/JTextHelper.h>
#include <j/graphics/VectorGraphics.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>

inline namespace jf {

namespace {

// As Swing's TableRowSorter: three sort keys at most.
constexpr size_t kMostSortKeys = 3;
// The second and third sort keys' marks fade by this much each (OpenPnP's
// MultisortTableHeaderCellRenderer).
constexpr float kSortKeyFade = 0.5f;
// A row's tick box, as a share of the row.
constexpr float kTickShare = 0.6f;
// Icons are drawn at twice their size and shown smaller, to stay sharp.
constexpr float kIconOversample = 2.0f;
// A tinted cell's fill: its colour, this opaque over the row.
constexpr float kHighlightAlpha = 0.5f;
// The widest number a decimal-aligned column lines up (OpenPnP's "%9.3f"
// and two places for units), split at its point.
constexpr const char* kAlignedWhole = "-00000";
constexpr const char* kAlignedRest  = ".000mm";

JColor colour(const uint8_t* c) { return rgb(c[0], c[1], c[2]); }

// As a collator orders text: by the letters first, without regard to case,
// then lower case before upper.
int collate(const std::string& a, const std::string& b) {
    const size_t n = std::min(a.size(), b.size());
    for (size_t i = 0; i < n; ++i) {
        const int ca = std::tolower(static_cast<unsigned char>(a[i])), cb = std::tolower(static_cast<unsigned char>(b[i]));
        if (ca != cb) return ca < cb ? -1 : 1;
    }
    if (a.size() != b.size()) return a.size() < b.size() ? -1 : 1;
    for (size_t i = 0; i < n; ++i)
        if (a[i] != b[i]) return std::islower(static_cast<unsigned char>(a[i])) ? -1 : 1;
    return 0;
}

// Text cut to `width`, ending in "..." where it is cut, as Swing cuts a cell.
std::string elided(const std::string& t, float width) {
    if (JTextHelper::measureWidth(t) <= width) return t;
    const std::string dots = "...";
    size_t lo = 0, hi = t.size();
    while (lo < hi) {   // the longest start that fits with the dots
        size_t mid = (lo + hi + 1) / 2;
        while (mid > 0 && mid < t.size() && (static_cast<unsigned char>(t[mid]) & 0xC0) == 0x80) --mid;
        if (mid <= lo) break;
        if (JTextHelper::measureWidth(t.substr(0, mid) + dots) <= width) lo = mid;
        else hi = mid - 1;
    }
    return t.substr(0, lo) + dots;
}

} // namespace

JPTable::JPTable(JSceneGraph& graph) : JControl(graph, "JPTable") {
    const JStyle& st = JStyle::current();
    auto& l = m_graph.getLayout(m_nodeId);
    l.minHeight = st.gridHeaderHeight + 2 * st.gridRowHeight;
    l.minWidth = 4 * st.gridMinColumnWidth;
    setHSizePolicy(JSizePolicyMode::Expanding, 1);
    setVSizePolicy(JSizePolicyMode::Expanding, 1);
    setContextMenu(nullptr);
    buildHeaderMenu(-1);
    s_tables.insert(this);
}

JPTable::~JPTable() {
    s_tables.erase(this);
    if (s_focusedTable == this) s_focusedTable = nullptr;
}

void JPTable::fieldsChanged() {
    for (JPTable* t : s_tables) {
        t->stopEditing(false);
        t->rebuildView();
    }
}

void JPTable::setModel(JPTableModel* model) {
    stopEditing(false);
    // Its rows' fields with it, where they have them.
    m_fieldsModel.reset();
    if (model && s_fields)
        if (const std::optional<JPFieldSet::Of> of = model->fieldsOf())
            m_fieldsModel = std::make_unique<JPFieldsModel>(*model, *s_fields, *of);
    m_model = m_fieldsModel ? m_fieldsModel.get() : model;
    m_widths.clear();
    m_sortKeys.clear();
    m_orderKeys.clear();   // the order made again for its columns, the kept state taken
    m_selected.clear();
    m_lead = m_anchor = -1;
    applySort();
    refresh();
}

void JPTable::setStateKey(const std::string& key) {
    m_stateKey = key;
    readViews(s_loadState ? s_loadState(key) : JJson());
    takeLayout(m_views[size_t(m_current)].layout);
    onViewsChanged.emit();
}

void JPTable::takeLayout(const JJson& layout) {
    endColumnDrag(false);
    stopEditing(true);
    m_state = layout;
    m_orderKeys.clear();
    m_widths.clear();
    m_sortKeys.clear();
    applySort();
    rebuildView();
}

void JPTable::resetColumns() {
    endColumnDrag(false);
    m_state = JJson();
    m_orderKeys.clear();
    m_widths.clear();
    m_sortKeys.clear();
    keepState();
    rebuildView();
}

// ---- views -------------------------------------------------------------------

void JPTable::readViews(const JJson& state) {
    m_views.clear();
    m_current = 0;
    m_viewChanged = false;
    for (const JJson& v : state["views"].arr()) {
        View view;
        view.name = v["name"].str();
        view.icon = v["icon"].str();
        view.all = v["all"].boolean(false);
        view.locked = v["locked"].boolean(false);
        view.keepSearch = v["keepSearch"].boolean(false);
        view.search = v["search"].str();
        view.layout = v["layout"];
        if (!view.name.empty()) m_views.push_back(std::move(view));
    }
    if (m_views.empty()) {
        // Standard (a layout kept before there were views, its own) and All Fields.
        View standard { "Standard", "list", false, false, false, "", state.contains("order") ? state : JJson() };
        View all { "All Fields", "columns", true, false, false, "", JJson() };
        m_views = { standard, all };
    }
    m_current = std::clamp(int(state["current"].number(0)), 0, int(m_views.size()) - 1);
}

void JPTable::saveViews() const {
    if (m_stateKey.empty() || !s_saveState) return;
    JJson j = JJson::object();
    JJson list = JJson::array();
    for (const View& v : m_views) {
        JJson o = JJson::object();
        o["name"] = v.name;
        o["icon"] = v.icon;
        if (v.all) o["all"] = true;
        if (v.locked) o["locked"] = true;
        if (v.keepSearch) {
            o["keepSearch"] = true;
            o["search"] = v.search;
        }
        o["layout"] = v.layout;
        list.push(o);
    }
    j["views"] = list;
    j["current"] = m_current;
    s_saveState(m_stateKey, j);
}

std::string JPTable::freeIcon() const {
    for (const JPViewIcons::Icon& i : JPViewIcons::all())
        if (std::none_of(m_views.begin(), m_views.end(), [&i](const View& v) { return v.icon == i.key; })) return i.key;
    return JPViewIcons::all().front().key;
}

void JPTable::showView(int i) {
    if (i < 0 || size_t(i) >= m_views.size() || (i == m_current && !m_viewChanged)) return;
    m_current = i;
    m_viewChanged = false;
    takeLayout(m_views[size_t(i)].layout);
    saveViews();
    if (m_views[size_t(i)].keepSearch) onSearchShown.emit(m_views[size_t(i)].search);
    onViewsChanged.emit();
}

int JPTable::addView(const std::string& name) {
    View v;
    v.name = name;
    v.icon = freeIcon();
    v.layout = layoutNow();   // as shown now, every column of All Fields' among them
    m_views.push_back(std::move(v));
    m_current = int(m_views.size()) - 1;
    m_viewChanged = false;
    saveViews();
    onViewsChanged.emit();
    return m_current;
}

void JPTable::renameView(int i, const std::string& name) {
    if (i < 0 || size_t(i) >= m_views.size() || name.empty()) return;
    m_views[size_t(i)].name = name;
    saveViews();
    onViewsChanged.emit();
}

void JPTable::setViewIcon(int i, const std::string& icon) {
    if (i < 0 || size_t(i) >= m_views.size()) return;
    m_views[size_t(i)].icon = icon;
    saveViews();
    onViewsChanged.emit();
}

int JPTable::duplicateView(int i) {
    if (i < 0 || size_t(i) >= m_views.size()) return -1;
    View v = m_views[size_t(i)];
    if (i == m_current) v.layout = layoutNow();   // as shown, changes and all
    v.all = false;
    v.name += " (copy)";
    v.icon = freeIcon();
    v.locked = false;
    m_views.insert(m_views.begin() + i + 1, std::move(v));
    m_current = i + 1;
    m_viewChanged = false;
    takeLayout(m_views[size_t(m_current)].layout);
    saveViews();
    onViewsChanged.emit();
    return m_current;
}

void JPTable::deleteView(int i) {
    if (i < 0 || size_t(i) >= m_views.size() || m_views.size() <= 1) return;
    m_views.erase(m_views.begin() + i);
    if (i == m_current) {
        m_current = std::min(i, int(m_views.size()) - 1);
        m_viewChanged = false;
        takeLayout(m_views[size_t(m_current)].layout);
    } else if (i < m_current) {
        --m_current;
    }
    saveViews();
    onViewsChanged.emit();
}

void JPTable::moveView(int from, int to) {
    if (from < 0 || size_t(from) >= m_views.size() || to < 0 || size_t(to) > m_views.size() || to == from || to == from + 1) return;
    const bool wasCurrent = from == m_current;
    View v = std::move(m_views[size_t(from)]);
    m_views.erase(m_views.begin() + from);
    const int at = to > from ? to - 1 : to;
    m_views.insert(m_views.begin() + at, std::move(v));
    if (wasCurrent) m_current = at;
    else if (from < m_current && at >= m_current) --m_current;
    else if (from > m_current && at <= m_current) ++m_current;
    saveViews();
    onViewsChanged.emit();
}

void JPTable::setViewLocked(int i, bool on) {
    if (i < 0 || size_t(i) >= m_views.size()) return;
    m_views[size_t(i)].locked = on;
    // Unlocked while changed: the changes are its own now.
    if (!on && i == m_current && m_viewChanged) {
        m_views[size_t(i)].layout = m_state;
        m_viewChanged = false;
    }
    saveViews();
    onViewsChanged.emit();
}

void JPTable::setViewKeepsSearch(int i, bool on) {
    if (i < 0 || size_t(i) >= m_views.size()) return;
    m_views[size_t(i)].keepSearch = on;
    if (on && i == m_current) m_views[size_t(i)].search = m_filterText;
    saveViews();
}

void JPTable::showInEveryView(const std::string& key) {
    for (size_t i = 0; i < m_views.size(); ++i) {
        View& v = m_views[i];
        if (v.all || int(i) == m_current) continue;
        // Out of its hidden, and among its order (one it never had would be hidden until chosen).
        JJson layout = v.layout.isObject() ? v.layout : JJson::object();
        JJson hidden = JJson::array();
        for (const JJson& h : std::as_const(layout)["hidden"].arr())
            if (h.str() != key) hidden.push(h);
        layout["hidden"] = hidden;
        JJson order = std::as_const(layout)["order"].isArray() ? std::as_const(layout)["order"] : JJson::array();
        bool known = false;
        for (const JJson& o : order.arr()) known = known || o.str() == key;
        if (!known) order.push(JJson(key));
        layout["order"] = order;
        v.layout = layout;
    }
    setColumnShown(key, true);   // the one shown, kept as it is (saved with the rest)
    saveViews();
}

void JPTable::revertView() {
    if (m_views.empty()) return;
    m_viewChanged = false;
    takeLayout(m_views[size_t(m_current)].layout);
    if (m_views[size_t(m_current)].keepSearch) onSearchShown.emit(m_views[size_t(m_current)].search);
    onViewsChanged.emit();
}

void JPTable::askViewName(const std::string& title, const std::string& now, std::function<void(const std::string&)> then) {
    JDialog::input(title, "What the view is called (its button's tooltip says it):",
                   [then = std::move(then), now](std::string name) {
                       while (!name.empty() && std::isspace(uint8_t(name.back()))) name.pop_back();
                       if (name.empty()) name = now;   // the name shown in the empty field
                       if (!name.empty()) then(name);
                   },
                   {}, now);
}

JMenu* JPTable::viewMenu(int i) {
    m_viewMenu = std::make_unique<JMenu>("View");
    m_iconMenu = std::make_unique<JMenu>("Icon");
    if (i < 0 || size_t(i) >= m_views.size()) return m_viewMenu.get();
    const View& v = m_views[size_t(i)];
    JMenuItem* show = m_viewMenu->add(m_graph, "Show \"" + v.name + "\"");
    show->setEnabled(i != m_current || m_viewChanged);
    show->onTriggered.connect([this, i] { showView(i); });
    m_viewMenu->addSeparator(m_graph);
    m_viewMenu->add(m_graph, "Rename…")->onTriggered.connect([this, i] {
        if (size_t(i) < m_views.size()) askViewName("Rename View", m_views[size_t(i)].name, [this, i](const std::string& n) { renameView(i, n); });
    });
    for (const JPViewIcons::Icon& icon : JPViewIcons::all()) {
        JMenuItem* item = m_iconMenu->add(m_graph, icon.label);
        item->setCheckable(true);
        item->setChecked(v.icon == icon.key);
        item->onTriggered.connect([this, i, key = std::string(icon.key)] { setViewIcon(i, key); });
    }
    m_viewMenu->add(m_graph, "Icon", {}, m_iconMenu.get())->setTooltip("The icon its button shows");
    m_viewMenu->add(m_graph, "Duplicate")->onTriggered.connect([this, i] { duplicateView(i); });
    JMenuItem* lock = m_viewMenu->add(m_graph, "Locked");
    lock->setCheckable(true);
    lock->setChecked(v.locked);
    lock->setTooltip("Keeps what it was made as: a change while it is shown lasts until another view is shown, or Revert View");
    lock->onTriggered.connect([this, i, lock] { setViewLocked(i, lock->isChecked()); });
    JMenuItem* search = m_viewMenu->add(m_graph, "Keeps Its Search");
    search->setCheckable(true);
    search->setChecked(v.keepSearch);
    search->setTooltip("Shown with the search it had (\"" + (v.keepSearch ? v.search : m_filterText) + "\")");
    search->onTriggered.connect([this, i, search] { setViewKeepsSearch(i, search->isChecked()); });
    if (i == m_current) {
        JMenuItem* revert = m_viewMenu->add(m_graph, "Revert View");
        revert->setEnabled(m_viewChanged);
        revert->setTooltip("Put back as it was kept, the changes made while it is shown undone");
        revert->onTriggered.connect([this] { revertView(); });
    }
    m_viewMenu->addSeparator(m_graph);
    JMenuItem* del = m_viewMenu->add(m_graph, "Delete View");
    del->setEnabled(m_views.size() > 1);
    del->setTooltip(m_views.size() > 1 ? "Taken away; the others stay" : "A table keeps one view at least");
    del->onTriggered.connect([this, i] { deleteView(i); });
    return m_viewMenu.get();
}

const std::vector<int>& JPTable::order() const {
    std::vector<std::string> keys;
    for (int c = 0; m_model && c < m_model->columnCount(); ++c) keys.push_back(JPTableModel::keyOf(m_model->column(c)));
    if (keys != m_orderKeys) {
        m_orderKeys = std::move(keys);
        m_order.clear();
        for (size_t c = 0; c < m_orderKeys.size(); ++c) m_order.push_back(int(c));
        m_widths.clear();
        m_hidden.clear();
        applyState();
        // A column hidden at first, that the kept layout has never had: hidden.
        std::set<std::string> known;
        if (m_state.isObject() && m_state["order"].isArray())
            for (const JJson& n : m_state["order"].arr())
                if (n.isString()) known.insert(n.str());
        for (int c = 0; m_model && c < m_model->columnCount(); ++c)
            if (m_model->column(c).hiddenAtFirst && !known.count(m_orderKeys[size_t(c)])) m_hidden.insert(m_orderKeys[size_t(c)]);
        // All Fields: every column, whatever was kept.
        if (!m_views.empty() && m_views[size_t(m_current)].all) m_hidden.clear();
        // A field added here: shown, after the column it was added after.
        bool placed = false;
        for (const auto& [key, after] : m_addedHere) {
            const auto at = std::find(m_orderKeys.begin(), m_orderKeys.end(), key);
            if (at == m_orderKeys.end()) continue;
            const int c = int(at - m_orderKeys.begin());
            m_hidden.erase(key);
            std::erase(m_order, c);
            auto to = m_order.end();
            for (auto it = m_order.begin(); it != m_order.end(); ++it)
                if (m_orderKeys[size_t(*it)] == after) to = it + 1;
            m_order.insert(to, c);
            placed = true;
        }
        if (placed) m_addedHere.clear();
        // The hidden ones out of what is shown, kept in their places.
        m_allOrder = m_order;
        std::erase_if(m_order, [this](int c) { return m_hidden.count(m_orderKeys[size_t(c)]) > 0; });
        if (m_order.empty() && !m_allOrder.empty()) {   // never none shown
            m_hidden.clear();
            m_order = m_allOrder;
        }
        if (placed) keepState();
    }
    return m_order;
}

std::vector<int> JPTable::allColumns() const {
    // What is shown in its order, each hidden one put back after the column before it in its last place.
    std::vector<int> out = order();
    for (size_t k = 0; k < m_allOrder.size(); ++k) {
        const int c = m_allOrder[k];
        if (std::find(out.begin(), out.end(), c) != out.end()) continue;
        auto at = out.begin();
        for (size_t j = k; j-- > 0;)
            if (const auto it = std::find(out.begin(), out.end(), m_allOrder[j]); it != out.end()) {
                at = it + 1;
                break;
            }
        out.insert(at, c);
    }
    return out;
}

std::vector<JPTable::ColumnShown> JPTable::columnsShown() const {
    std::vector<ColumnShown> out;
    for (const int c : allColumns())
        out.push_back({ m_orderKeys[size_t(c)], m_model->column(c).name, m_hidden.count(m_orderKeys[size_t(c)]) == 0 });
    return out;
}

void JPTable::setColumnShown(const std::string& key, bool shown) {
    const std::string& name = key;
    endColumnDrag(false);
    stopEditing(true);
    const std::vector<int> all = allColumns();
    const bool known = std::any_of(all.begin(), all.end(), [&](int c) { return m_orderKeys[size_t(c)] == name; });
    if (!known || shown == (m_hidden.count(name) == 0)) return;
    if (!shown && !m_views.empty() && m_views[size_t(m_current)].all) return;   // All Fields shows every one
    if (!shown && order().size() <= 1) return;   // the last one shown stays
    m_allOrder = all;
    if (shown) {
        // Shown again as wide as the others shown are on the whole (its own width, never laid out while it was
        // hidden, says nothing); the others give it the room.
        materialiseWidths();
        float sum = 0;
        int counted = 0;
        for (const int c : order())
            if (m_model->column(c).width <= 0 && size_t(c) < m_widths.size()) {
                sum += m_widths[size_t(c)];
                ++counted;
            }
        for (int c = 0; c < int(m_orderKeys.size()); ++c)
            if (m_orderKeys[size_t(c)] == name && size_t(c) < m_widths.size() && m_model->column(c).width <= 0)
                m_widths[size_t(c)] = counted ? sum / float(counted) : JStyle::current().gridDefaultColumnWidth;
        m_hidden.erase(name);
    } else {
        m_hidden.insert(name);
    }
    m_order.clear();
    for (const int c : m_allOrder)
        if (!m_hidden.count(m_orderKeys[size_t(c)])) m_order.push_back(c);
    keepState();
    m_graph.invalidateNode(m_nodeId, DirtySelf);
}

JMenu* JPTable::columnsMenu() {
    m_columnsMenu = std::make_unique<JMenu>("Columns");
    // The views: each one (the one shown ticked), a new one, the one shown's own actions (a table with tools).
    for (size_t i = 0; m_hasTools && i < m_views.size(); ++i) {
        JMenuItem* item = m_columnsMenu->add(m_graph, m_views[i].name + (int(i) == m_current && m_viewChanged ? " (changed)" : ""));
        item->setCheckable(true);
        item->setChecked(int(i) == m_current);
        if (i < 9) item->setTooltip("Ctrl+" + std::to_string(i + 1) + " shows it");
        item->onTriggered.connect([this, i] { showView(int(i)); });
    }
    if (m_hasTools) {
        JMenuItem* newView = m_columnsMenu->add(m_graph, "New View…");
        newView->setTooltip("A view of this table made from the one shown: its columns, their order, widths and sort, kept under a name");
        newView->onTriggered.connect([this] { askViewName("New View", "", [this](const std::string& n) { addView(n); }); });
        m_columnsMenu->add(m_graph, "This View", {}, viewMenu(m_current));
        m_columnsMenu->addSeparator(m_graph);
    }
    const bool everyField = !m_views.empty() && m_views[size_t(m_current)].all;
    const std::vector<ColumnShown> columns = columnsShown();
    const size_t shownCount = size_t(std::count_if(columns.begin(), columns.end(), [](const ColumnShown& c) { return c.shown; }));
    for (const ColumnShown& c : columns) {
        // A column without a title (an icon's) by what its tooltip says it is, before its colon.
        std::string label = c.name;
        for (int k = 0; label.empty() && m_model && k < m_model->columnCount(); ++k)
            if (JPTableModel::keyOf(m_model->column(k)) == c.key)
                label = m_model->column(k).tooltip.substr(0, m_model->column(k).tooltip.find(':'));
        JMenuItem* item = m_columnsMenu->add(m_graph, label);
        item->setCheckable(true);
        item->setChecked(c.shown);
        if (c.shown && shownCount <= 1) {
            item->setEnabled(false);
            item->setTooltip("The last column shown stays");
        }
        if (everyField) {
            item->setEnabled(false);
            item->setTooltip("All Fields shows every column: another view hides some");
        }
        // The tick has already flipped when this runs: it says what is wanted.
        item->onTriggered.connect([this, item, key = c.key] { setColumnShown(key, item->isChecked()); });
    }
    m_columnsMenu->addSeparator(m_graph);
    if (const std::optional<JPFieldSet::Of> of = m_fieldsModel && m_hasTools && m_offersFields ? m_fieldsModel->fieldsOf() : std::nullopt) {
        const std::string kind = JPFieldSet::plural(*of);
        JMenuItem* add = m_columnsMenu->add(m_graph, "Add Field…");
        add->setTooltip("A field of your own for " + kind + ": shown here, offered by every table of " + kind
                        + " and by the import");
        add->setEnabled(bool(s_fieldHooks.add));
        add->onTriggered.connect([this, of] {
            if (s_fieldHooks.add) s_fieldHooks.add(*of, std::string(), [this](const std::string& key) { fieldAdded(key, std::string()); });
        });
        JMenuItem* edit = m_columnsMenu->add(m_graph, "Edit Fields…");
        edit->setTooltip("Every field of " + kind + ": rename one, change what an import matches it by, add one, delete "
                         "one (those jplacer needs stay)");
        edit->setEnabled(bool(s_fieldHooks.manage));
        edit->onTriggered.connect([of] {
            if (s_fieldHooks.manage) s_fieldHooks.manage(*of);
        });
        m_columnsMenu->addSeparator(m_graph);
    }
    JMenuItem* reset = m_columnsMenu->add(m_graph, "Reset Columns");
    reset->setTooltip("Put this table's columns back as they first were: every one shown, their order, their widths, "
                      "no sort");
    reset->onTriggered.connect([this] { resetColumns(); });
    return m_columnsMenu.get();
}

int JPTable::positionOf(int c) const {
    const std::vector<int>& o = order();
    const auto it = std::find(o.begin(), o.end(), c);
    return it == o.end() ? -1 : int(it - o.begin());
}

void JPTable::applyState() const {
    const JJson& state = m_state;   // read only: a missing key is not made
    if (!state.isObject()) return;
    std::map<std::string, int> byName;
    for (size_t c = 0; c < m_orderKeys.size(); ++c) byName.emplace(m_orderKeys[c], int(c));
    // The columns hidden, by name.
    if (state["hidden"].isArray())
        for (const JJson& n : state["hidden"].arr())
            if (n.isString() && byName.count(n.str())) m_hidden.insert(n.str());
    // The order kept, by name; a column it does not name goes after the one before it in the model's order.
    if (state["order"].isArray()) {
        std::vector<int> o;
        for (const JJson& n : state["order"].arr())
            if (n.isString())
                if (const auto it = byName.find(n.str()); it != byName.end() && std::find(o.begin(), o.end(), it->second) == o.end())
                    o.push_back(it->second);
        for (int c = 0; c < int(m_orderKeys.size()); ++c) {
            if (std::find(o.begin(), o.end(), c) != o.end()) continue;
            auto at = o.begin();
            for (int before = c - 1; before >= 0; --before)
                if (const auto it = std::find(o.begin(), o.end(), before); it != o.end()) {
                    at = it + 1;
                    break;
                }
            o.insert(at, c);
        }
        m_order = std::move(o);
    }
    // The widths kept, as shares of the whole (a column without one takes the average); laid out to the
    // table's width as it is (materialiseWidths).
    if (state["widths"].isObject()) {
        std::vector<float> shares(m_orderKeys.size(), 0.f);
        float sum = 0;
        int known = 0;
        for (size_t c = 0; c < m_orderKeys.size(); ++c)
            if (const JJson& w = state["widths"][m_orderKeys[c]]; w.isNumber() && w.number() > 0) {
                shares[c] = float(w.number());
                sum += shares[c];
                ++known;
            }
        if (known > 0) {
            const float average = sum / float(known);
            for (float& w : shares)
                if (w <= 0) w = average;
            // A column of the model's own width (an icon's) has it; the shares are the others'.
            for (size_t c = 0; c < shares.size(); ++c)
                if (const float own = m_model->column(int(c)).width; own > 0) shares[c] = own;
            m_widths = std::move(shares);
        }
    }
}

void JPTable::applySort() {
    const JJson& state = m_state;
    if (!m_model || !state.isObject() || !state["sort"].isArray()) return;
    for (const JJson& k : state["sort"].arr()) {
        if (!k["column"].isString()) continue;
        for (int c = 0; c < m_model->columnCount(); ++c)
            if (JPTableModel::keyOf(m_model->column(c)) == k["column"].str() && m_sortKeys.size() < kMostSortKeys)
                m_sortKeys.push_back({ c, !k["ascending"].isBool() || k["ascending"].boolean() });
    }
}

JJson JPTable::layoutNow() const {
    JJson st = JJson::object();
    if (!m_model) return st;
    JJson o = JJson::array();
    for (const int c : allColumns()) o.push(JJson(m_orderKeys[size_t(c)]));
    st["order"] = o;
    JJson hidden = JJson::array();
    for (const std::string& name : m_hidden) hidden.push(JJson(name));
    st["hidden"] = hidden;
    const float total = totalColumnsWidth();
    if (total > 0) {
        // The columns shown, as shares of the whole: a hidden one is not laid out, so its width says nothing
        // (it takes the others' average when it comes back).
        JJson w = JJson::object();
        for (const int c : order())
            if (size_t(c) < m_widths.size()) w[m_orderKeys[size_t(c)]] = double(m_widths[size_t(c)] / total);
        st["widths"] = w;
    }
    JJson sort = JJson::array();
    for (const SortKey& k : m_sortKeys)
        if (k.column >= 0 && size_t(k.column) < m_orderKeys.size()) {
            JJson key = JJson::object();
            key["column"] = m_orderKeys[size_t(k.column)];
            key["ascending"] = k.ascending;
            sort.push(key);
        }
    st["sort"] = sort;
    return st;
}

void JPTable::keepState() const {
    if (!m_model) return;
    const JJson st = layoutNow();
    m_state = st;
    if (m_views.empty()) return;
    // The one shown as it is now; a locked one only shows it, changed.
    View& v = m_views[size_t(m_current)];
    if (v.locked) {
        if (!m_viewChanged) {
            m_viewChanged = true;
            const_cast<JPTable*>(this)->onViewsChanged.emit();
        }
        return;
    }
    v.layout = st;
    saveViews();
}

void JPTable::refresh() {
    std::set<std::string> chosen;
    for (const int r : m_selected)
        if (r >= 0 && size_t(r) < m_keys.size()) chosen.insert(m_keys[size_t(r)]);
    const std::string lead = m_lead >= 0 && size_t(m_lead) < m_keys.size() ? m_keys[size_t(m_lead)] : std::string();
    m_keys.clear();
    const int n = m_model ? m_model->rowCount() : 0;
    for (int r = 0; r < n; ++r) m_keys.push_back(m_model->rowKey(r));
    m_selected.clear();
    m_lead = m_anchor = -1;
    for (int r = 0; r < n; ++r) {
        if (chosen.count(m_keys[size_t(r)])) m_selected.insert(r);
        if (!lead.empty() && m_keys[size_t(r)] == lead) m_lead = m_anchor = r;
    }
    if (m_editing && (m_editRow >= n)) stopEditing(false);
    rebuildView();
}

bool JPTable::rowPasses(int row) const {
    if (!m_model->rowShown(row)) return false;
    if (!searching()) return true;
    if (!m_filterKey.empty()) {
        // One column: what is in it matched (nothing asked: anything in it).
        for (int c = 0; c < m_model->columnCount(); ++c)
            if (JPTableModel::keyOf(m_model->column(c)) == m_filterKey) {
                const std::string t = m_model->column(c).kind == JPTableModel::Kind::Boolean ? (m_model->checked(row, c) ? "yes" : "")
                                                                                              : m_model->text(row, c);
                return m_filter ? std::regex_search(t, *m_filter) : !t.empty();
            }
        return false;
    }
    // Any column: one shown first; one hidden noted, for the row's mark and tooltip.
    const std::vector<int>& shown = order();
    for (const int c : shown)
        if (std::regex_search(m_model->text(row, c), *m_filter)) return true;
    for (int c = 0; c < m_model->columnCount(); ++c)
        if (std::find(shown.begin(), shown.end(), c) == shown.end() && std::regex_search(m_model->text(row, c), *m_filter)) {
            const JPTableModel::Column col = m_model->column(c);
            m_hiddenHits[row] = col.name.empty() ? col.tooltip.substr(0, col.tooltip.find(':')) : col.name;
            return true;
        }
    return false;
}

int JPTable::unsearchedRowCount() const {
    int n = 0;
    for (int r = 0; m_model && r < m_model->rowCount(); ++r) n += m_model->rowShown(r);
    return n;
}

std::vector<std::string> JPTable::columnNames() const {
    std::vector<std::string> out;
    for (int c = 0; m_model && c < m_model->columnCount(); ++c) {
        const JPTableModel::Column col = m_model->column(c);
        const std::string name = col.name.empty() ? col.tooltip.substr(0, col.tooltip.find(':')) : col.name;
        if (!name.empty()) out.push_back(name);
    }
    return out;
}

int JPTable::compareRows(int a, int b, int c) const {
    if (const std::optional<int> o = m_model->compare(a, b, c)) return *o;
    switch (m_model->column(c).kind) {
        case JPTableModel::Kind::Number: {
            const double x = m_model->number(a, c), y = m_model->number(b, c);
            return x < y ? -1 : x > y ? 1 : 0;
        }
        case JPTableModel::Kind::Boolean: {
            const bool x = m_model->checked(a, c), y = m_model->checked(b, c);
            return x == y ? 0 : (x ? 1 : -1);
        }
        default: return collate(m_model->text(a, c), m_model->text(b, c));
    }
}

void JPTable::rebuildView() {
    m_view.clear();
    m_hiddenHits.clear();
    if (m_model)
        for (int r = 0; r < m_model->rowCount(); ++r)
            if (rowPasses(r)) m_view.push_back(r);
    if (m_model && !m_sortKeys.empty())
        std::stable_sort(m_view.begin(), m_view.end(), [this](int a, int b) {
            for (const SortKey& k : m_sortKeys) {
                if (k.column >= m_model->columnCount()) continue;
                const int c = compareRows(a, b, k.column);
                if (c != 0) return k.ascending ? c < 0 : c > 0;
            }
            return a < b;
        });
    // Rows the filter hides are no longer chosen, as in Swing.
    std::erase_if(m_selected, [this](int r) { return viewIndexOf(r) < 0; });
    if (m_lead >= 0 && viewIndexOf(m_lead) < 0) m_lead = -1;
    clampScroll();
    m_graph.invalidateNode(m_nodeId, DirtySelf);
    onViewChanged.emit();
}

void JPTable::setFilter(const std::string& text) {
    auto trim = [](std::string t) {
        while (!t.empty() && std::isspace(static_cast<unsigned char>(t.back()))) t.pop_back();
        size_t a = 0;
        while (a < t.size() && std::isspace(static_cast<unsigned char>(t[a]))) ++a;
        return t.substr(a);
    };
    std::string t = trim(text);
    // "name: text": a column named so (without regard to case), searched alone.
    std::string key;
    if (const size_t colon = t.find(':'); colon != std::string::npos && m_model) {
        std::string name = trim(t.substr(0, colon));
        for (char& ch : name) ch = char(std::tolower(uint8_t(ch)));
        for (int c = 0; c < m_model->columnCount() && key.empty(); ++c) {
            const JPTableModel::Column col = m_model->column(c);
            std::string heading = col.name.empty() ? col.tooltip.substr(0, col.tooltip.find(':')) : col.name;
            for (char& ch : heading) ch = char(std::tolower(uint8_t(ch)));
            if (!name.empty() && heading == name) key = JPTableModel::keyOf(col);
        }
        if (!key.empty()) t = trim(t.substr(colon + 1));
    }
    std::optional<std::regex> filter;
    if (!t.empty()) {
        try {
            filter = std::regex(t, std::regex::ECMAScript | std::regex::icase);
        } catch (const std::regex_error&) {
            return;   // as OpenPnP: an expression not finished yet changes nothing
        }
    }
    m_filter = std::move(filter);
    m_filterKey = key;
    m_filterText = text;
    // A view keeping its search keeps it as it is now (a locked one only shows it).
    if (!m_views.empty() && m_views[size_t(m_current)].keepSearch && !m_views[size_t(m_current)].locked
        && m_views[size_t(m_current)].search != text) {
        m_views[size_t(m_current)].search = text;
        saveViews();
    }
    const size_t before = m_selected.size();
    rebuildView();
    if (m_selected.size() != before) selectionChanged();
}

int JPTable::viewIndexOf(int modelRow) const {
    const auto it = std::find(m_view.begin(), m_view.end(), modelRow);
    return it == m_view.end() ? -1 : int(it - m_view.begin());
}

std::vector<int> JPTable::selectedRows() const {
    std::vector<int> out;
    for (const int r : m_view)
        if (m_selected.count(r)) out.push_back(r);
    return out;
}

int JPTable::selectedRow() const {
    return m_selected.size() == 1 ? *m_selected.begin() : -1;
}

void JPTable::selectRow(int modelRow) {
    selectRows(modelRow >= 0 ? std::vector<int> { modelRow } : std::vector<int> {});
}

void JPTable::selectRows(const std::vector<int>& rows) {
    std::set<int> now;
    for (const int r : rows)
        if (viewIndexOf(r) >= 0) now.insert(r);
    const bool changed = now != m_selected;
    m_selected = std::move(now);
    m_lead = m_anchor = m_selected.empty() ? -1 : rows.front();
    if (m_lead >= 0) ensureVisible(viewIndexOf(m_lead));
    m_graph.invalidateNode(m_nodeId, DirtySelf);
    if (changed) onSelectionChanged.emit();
}

void JPTable::clearSelection() {
    selectRows({});
}

void JPTable::selectionChanged() {
    m_graph.invalidateNode(m_nodeId, DirtySelf);
    onSelectionChanged.emit();
}

// ---- geometry ---------------------------------------------------------------

JRect JPTable::bounds() const {
    return m_graph.getLayoutConst(m_nodeId).boundingBox;
}

float JPTable::headerHeight() const { return m_headerShown ? JStyle::current().gridHeaderHeight : 0.f; }

void JPTable::scrollToEnd() {
    m_scrollY = float(m_view.size()) * rowHeight();
    clampScroll();
    invalidate();
}

bool JPTable::atEnd() const {
    const JRect b = bounds();
    const float maxY = std::max(0.f, float(m_view.size()) * rowHeight() - (b.height - headerHeight()));
    return m_scrollY >= maxY - rowHeight() * 0.5f;
}
float JPTable::rowHeight() const { return JStyle::current().gridRowHeight; }

void JPTable::materialiseWidths() const {
    // As a Swing table (AUTO_RESIZE_SUBSEQUENT_COLUMNS): the columns always
    // fill the table's width; a new width shares itself out as they stood.
    if (!m_model) return;
    order();   // the model's columns as they are now, a kept width taken
    const int n = m_model->columnCount();
    const JStyle& st = JStyle::current();
    const float room = bounds().width - st.scrollBarWidth;
    if (room <= 0 || n == 0) return;
    if (int(m_widths.size()) != n) {
        m_widths.assign(size_t(n), 0);
        float fixed = 0;
        int shared = 0;
        for (int c = 0; c < n; ++c) {
            const float w = m_model->column(c).width;
            if (w > 0) fixed += w;
            else ++shared;
        }
        for (int c = 0; c < n; ++c) {
            const float w = m_model->column(c).width;
            m_widths[size_t(c)] = w > 0 ? w : (shared ? std::max(0.f, room - fixed) / float(shared) : st.gridDefaultColumnWidth);
        }
    }
    // A column the model gives a width (an icon's) keeps it; the others shown share out the rest (a hidden
    // one keeps its width for when it is shown again).
    float total = 0, fixed = 0;
    for (const int c : m_order) {
        total += m_widths[size_t(c)];
        if (m_model->column(c).width > 0) fixed += m_widths[size_t(c)];
    }
    const float shared = total - fixed, shareRoom = room - fixed;
    if (shared > 0 && shareRoom > 0 && std::fabs(total - room) > 0.5f)
        for (const int c : m_order)
            if (m_model->column(c).width <= 0)
                m_widths[size_t(c)] = std::max(st.gridMinColumnWidth, m_widths[size_t(c)] * shareRoom / shared);
}

float JPTable::columnWidth(int c) const {
    materialiseWidths();
    return c >= 0 && size_t(c) < m_widths.size() ? m_widths[size_t(c)] : 0;
}

void JPTable::setColumnWidth(int c, float w) {
    // The columns after it (as shown) give or take the difference, as in Swing.
    materialiseWidths();
    const std::vector<int>& o = order();
    const int at = positionOf(c);
    if (at < 0 || size_t(at) + 1 >= o.size() || m_widths.size() != m_orderKeys.size()) return;
    const float least = JStyle::current().gridMinColumnWidth;
    float after = 0;
    for (size_t i = size_t(at) + 1; i < o.size(); ++i) after += m_widths[size_t(o[i])];
    const float most = m_widths[size_t(c)] + after - least * float(o.size() - size_t(at) - 1);
    const float now = std::clamp(w, least, std::max(least, most));
    const float give = now - m_widths[size_t(c)];
    m_widths[size_t(c)] = now;
    for (size_t i = size_t(at) + 1; i < o.size(); ++i) {
        float& x = m_widths[size_t(o[i])];
        x = std::max(least, x - give * (after > 0 ? x / after : 0));
    }
    m_graph.invalidateNode(m_nodeId, DirtySelf);
}

float JPTable::totalColumnsWidth() const {
    materialiseWidths();
    float w = 0;
    for (const int c : order())
        if (size_t(c) < m_widths.size()) w += m_widths[size_t(c)];
    return w;
}

float JPTable::columnX(int c) const {
    materialiseWidths();
    float x = 0;
    for (const int o : order()) {
        if (o == c) break;
        if (size_t(o) < m_widths.size()) x += m_widths[size_t(o)];
    }
    return x;
}

int JPTable::columnAt(float mx) const {
    const JRect b = bounds();
    float x = b.x - m_scrollX;
    for (const int c : order()) {
        const float w = columnWidth(c);
        if (mx >= x && mx < x + w) return c;
        x += w;
    }
    return -1;
}

int JPTable::dividerAt(float mx, float my) const {
    const JRect b = bounds();
    if (!m_model || my < b.y || my >= b.y + headerHeight()) return -1;
    float x = b.x - m_scrollX;
    for (const int c : order()) {
        x += columnWidth(c);
        if (std::fabs(mx - x) <= JStyle::current().gridResizeGrab) return c;
    }
    return -1;
}

int JPTable::viewRowAt(float my) const {
    const JRect b = bounds();
    const float y = my - b.y - headerHeight() + m_scrollY;
    if (my < b.y + headerHeight() || y < 0) return -1;
    const int v = int(y / rowHeight());
    return v < int(m_view.size()) ? v : -1;
}

int JPTable::rowAt(float my) const {
    const int v = viewRowAt(my);
    return v >= 0 ? m_view[size_t(v)] : -1;
}

JRect JPTable::cellRect(int viewRow, int c) const {
    const JRect b = bounds();
    return { b.x + columnX(c) - m_scrollX, b.y + headerHeight() + float(viewRow) * rowHeight() - m_scrollY, columnWidth(c),
             rowHeight() };
}

void JPTable::clampScroll() {
    const JRect b = bounds();
    const float sb = JStyle::current().scrollBarWidth;
    (void)sb;
    const float maxY = std::max(0.f, float(m_view.size()) * rowHeight() - (b.height - headerHeight()));
    m_scrollY = std::clamp(m_scrollY, 0.f, maxY);
    m_scrollX = 0;   // the columns always fit the width
}

void JPTable::ensureVisible(int v) {
    if (v < 0) return;
    const JRect b = bounds();
    const float visible = b.height - headerHeight();
    // Not laid out yet (its tab behind another): brought into view when it is.
    if (visible < rowHeight()) {
        m_reveal = m_view[size_t(v)];
        return;
    }
    const float top = float(v) * rowHeight();
    if (top < m_scrollY) m_scrollY = top;
    else if (top + rowHeight() > m_scrollY + visible) m_scrollY = top + rowHeight() - visible;
    clampScroll();
    m_graph.invalidateNode(m_nodeId, DirtySelf);
}

// ---- drawing ----------------------------------------------------------------

void JPTable::populateRenderPrimitives(JPrimitiveBuffer& buf) {
    const JRect b = bounds();
    const JStyle& st = JStyle::current();
    const bool focused = isFocused();
    buf.pushRectangle(b.x, b.y, b.width, b.height, Colors::Surface1, st.cornerRadius, st.borderWidth,
                      focused ? Colors::Accent : Colors::Border);
    if (!m_model || !JTextHelper::hasAtlas()) return;
    if (m_reveal >= 0) {
        const int r = m_reveal;
        m_reveal = -1;
        ensureVisible(viewIndexOf(r));
    }
    clampScroll();
    const float hh = headerHeight(), rh = rowHeight(), pad = st.gridCellPadding;
    const float sb = st.scrollBarWidth;
    const float innerW = b.width - sb, innerH = b.height;
    const float lh = JTextHelper::lineHeight();

    const std::vector<int> shownOrder = order();
    const int dragged = m_columnDrag ? m_headerPress : -1;
    // A column dragged: where it floats (with the pointer), and a gap where it would go.
    const float floatX = m_dragX - m_grabOffset;

    // A heading: its name centred, its sort mark.
    auto heading = [&](int c, float x, float w) {
        float fade = 1.f;
        int keyIndex = -1;
        for (size_t k = 0; k < m_sortKeys.size(); ++k) {
            if (m_sortKeys[k].column == c) {
                keyIndex = int(k);
                break;
            }
            fade *= kSortKeyFade;
        }
        const float reserve = keyIndex >= 0 ? st.gridSortGlyphWidth : 0.f;
        // Centred, as a Swing table's headings are.
        const float room = std::max(1.f, w - 2 * pad - reserve);
        const std::string name = elided(m_model->column(c).name, room);
        const float tx = x + pad + std::max(0.f, (room - JTextHelper::measureWidth(name)) * 0.5f);
        JTextHelper::pushText(buf, tx, b.y + (hh - lh) * 0.5f, name, Colors::GridHeaderText, room);
        if (keyIndex >= 0) {
            uint8_t ink[4] = { Colors::GridHeaderText[0], Colors::GridHeaderText[1], Colors::GridHeaderText[2],
                               uint8_t(float(Colors::GridHeaderText[3]) * fade) };
            JArrow::draw(buf, x + w - pad - st.gridSortGlyphWidth * 0.5f, b.y + hh * 0.5f,
                         m_sortKeys[size_t(keyIndex)].ascending ? JArrow::Direction::Up : JArrow::Direction::Down, ink);
        }
    };

    // The headings, each with its sort marks.
    buf.pushClip(b.x, b.y, innerW, innerH);
    buf.pushRectangle(b.x, b.y, innerW, hh, Colors::Surface3);
    for (size_t pos = 0; pos < (m_headerShown ? shownOrder.size() : 0); ++pos) {
        const int c = shownOrder[pos];
        const float x = b.x + columnX(c) - m_scrollX, w = columnWidth(c);
        if (pos > 0) buf.pushRectangle(x, b.y, st.borderWidth, hh, Colors::Border);
        if (c == dragged) {
            buf.pushRectangle(x, b.y, w, hh, Colors::Surface2);   // its place, open for it
            continue;
        }
        heading(c, x, w);
    }
    buf.popClip();

    // What a search matched in a cell's text as drawn at `tx`: marked behind it.
    const bool marking = m_filter.has_value();
    auto markMatch = [&](int c, float tx, float ty, const std::string& shown) {
        if (!marking || shown.empty()) return;
        if (!m_filterKey.empty() && JPTableModel::keyOf(m_model->column(c)) != m_filterKey) return;
        std::smatch m;
        if (!std::regex_search(shown, m, *m_filter) || m.length(0) == 0) return;
        const float x0 = tx + JTextHelper::measureWidth(shown.substr(0, size_t(m.position(0))));
        const float x1 = tx + JTextHelper::measureWidth(shown.substr(0, size_t(m.position(0) + m.length(0))));
        uint8_t hit[4] = { Colors::Accent[0], Colors::Accent[1], Colors::Accent[2], 110 };
        buf.pushRectangle(x0, ty, std::max(1.f, x1 - x0), lh, hit);
    };
    // A cell's contents (its tick box, tint, icon and text) in `cell`.
    JVectorCanvas vg;
    auto content = [&](JVectorCanvas& canvas, int r, int c, const JRect& cell, bool chosen) {
        const float y = cell.y;
        const JPTableModel::Kind kind = m_model->column(c).kind;
        if (kind == JPTableModel::Kind::Boolean) {
            const float s = rh * kTickShare;
            const float bx = cell.x + (cell.width - s) * 0.5f, by = y + (rh - s) * 0.5f;
            const bool dimmed = !chosen && m_model->cellDimmed(r, c);
            canvas.strokeRoundedRect(bx, by, s, s, st.borderWidth * 2, st.borderWidth,
                                     JPaint::solid(colour(dimmed ? Colors::MutedText : Colors::Border)));
            if (m_model->checked(r, c)) {
                canvas.fillRoundedRect(bx, by, s, s, st.borderWidth * 2, JPaint::solid(colour(dimmed ? Colors::MutedText : Colors::Accent)));
                std::vector<JVectorCanvas::JVec2> tick { { bx + s * 0.22f, by + s * 0.52f }, { bx + s * 0.42f, by + s * 0.72f },
                                                         { bx + s * 0.78f, by + s * 0.3f } };
                canvas.strokePolyline(tick, st.borderWidth * 2, JPaint::solid(colour(Colors::HighlightedText)));
            }
            return;
        }
        const JPTableModel::Column col = m_model->column(c);
        const uint8_t* ink = Colors::LabelText;
        if (const uint8_t* fill = m_model->cellTint(r, c)) {
            const JColor tint = rgba(fill[0], fill[1], fill[2], uint8_t(float(fill[3]) * kHighlightAlpha));
            buf.pushRectangle(cell.x + st.borderWidth, y, cell.width - st.borderWidth, rh - st.borderWidth, tint.data());
            ink = Colors::TextPrimary;
        }
        if (const uint8_t* own = m_model->cellInk(r, c)) ink = own;
        float tx = cell.x + pad;
        const std::string icon = m_model->cellIcon(r, c);
        // An icon before the text, as tall as a line (alone in its cell: in its middle).
        if (!icon.empty())
            if (JPOpenPnpIcons* icons = JPOpenPnpIcons::instance()) {
                const std::string shown = m_model->displayText(r, c);
                // Its leading spaces (an indent) before the icon.
                const size_t lead = shown.find_first_not_of(' ');
                if (lead == std::string::npos) tx = std::max(tx, cell.x + (cell.width - lh) * 0.5f);
                else tx += JTextHelper::measureWidth(shown.substr(0, lead));
                const TextureHandle tex = icons->texture(icon, int(lh * kIconOversample), false);
                if (tex != kNullTexture) buf.pushImage(tx, y + (rh - lh) * 0.5f, lh, lh, tex);
                tx += lh + st.spacing * 0.5f;
            }
        const float room = std::max(1.f, cell.x + cell.width - pad - tx);
        std::string full = m_model->displayText(r, c);
        if (!icon.empty()) {
            const size_t lead = full.find_first_not_of(' ');
            full = lead == std::string::npos ? std::string() : full.substr(lead);
        }
        if (full.empty()) return;
        if (col.decimalAligned) {
            // The point at the same place in every row, the widest number centred.
            const size_t dot = full.find('.');
            const std::string whole = full.substr(0, dot);
            const float wholeW = JTextHelper::measureWidth(kAlignedWhole);
            const float boxW = wholeW + JTextHelper::measureWidth(kAlignedRest);
            if (boxW <= cell.width - 2 * pad) {
                const float dotX = cell.x + (cell.width - boxW) * 0.5f + wholeW;
                tx = std::max(tx, dotX - JTextHelper::measureWidth(whole));
            } else {
                // Too narrow to line up: against the right, as a number.
                tx = std::max(tx, cell.x + cell.width - pad - JTextHelper::measureWidth(full));
            }
            // A hair of slack: the room is worked out from the same text's width.
            const float fit = cell.x + cell.width - pad - tx + 1.f;
            const std::string shown = elided(full, fit);
            markMatch(c, tx, y + (rh - lh) * 0.5f, shown);
            JTextHelper::pushText(buf, tx, y + (rh - lh) * 0.5f, shown, ink, std::max(1.f, fit));
            return;
        }
        const std::string t = elided(full, room);
        const float tw = JTextHelper::measureWidth(t);
        JPTableModel::Align align = col.align;
        if (align == JPTableModel::Align::Auto)
            align = kind == JPTableModel::Kind::Number ? JPTableModel::Align::Right : JPTableModel::Align::Left;
        if (align == JPTableModel::Align::Right) tx = std::max(tx, cell.x + cell.width - pad - tw);
        else if (align == JPTableModel::Align::Center) tx = std::max(tx, cell.x + (cell.width - tw) * 0.5f);
        markMatch(c, tx, y + (rh - lh) * 0.5f, t);
        JTextHelper::pushText(buf, tx, y + (rh - lh) * 0.5f, t, ink, room);
    };

    // The rows in view.
    buf.pushClip(b.x, b.y + hh, innerW, innerH - hh);
    const int first = std::max(0, int(m_scrollY / rh));
    const int last = std::min(int(m_view.size()) - 1, int((m_scrollY + innerH - hh) / rh) + 1);
    for (int v = first; v <= last; ++v) {
        const int r = m_view[size_t(v)];
        const float y = b.y + hh + float(v) * rh - m_scrollY;
        const bool chosen = m_selected.count(r) != 0;
        if (s_alternateRows && v % 2 == 1) buf.pushRectangle(b.x, y, innerW, rh, Colors::RowAltBg);
        if (chosen) {
            uint8_t sel[4] = { Colors::Accent[0], Colors::Accent[1], Colors::Accent[2], 90 };
            buf.pushRectangle(b.x, y, innerW, rh, sel);
        }
        // Found only in a column not shown: marked at its start.
        if (m_hiddenHits.count(r)) buf.pushRectangle(b.x + st.borderWidth, y, st.borderWidth * 3, rh, Colors::Accent);
        for (size_t pos = 0; pos < shownOrder.size(); ++pos) {
            const int c = shownOrder[pos];
            const JRect cell = cellRect(v, c);
            if (pos > 0) buf.pushRectangle(cell.x, y, st.borderWidth, rh, Colors::GridLine);
            if (c == dragged) {
                buf.pushRectangle(cell.x, y, cell.width, rh, Colors::Surface2);   // its place, open for it
                continue;
            }
            if (m_editing && r == m_editRow && c == m_editColumn) continue;   // drawn below
            content(vg, r, c, cell, chosen);
        }
        buf.pushRectangle(b.x, y + rh - st.borderWidth, innerW, st.borderWidth, Colors::GridLine);
    }
    vg.flush(buf);
    buf.popClip();

    // The dragged column, floating with the pointer over the table as it would look: its heading and its cells,
    // raised as a dragged tab is.
    if (dragged >= 0) {
        const float w = columnWidth(dragged);
        buf.pushClip(b.x, b.y, innerW, innerH);
        buf.pushRectangle(floatX, b.y, w, innerH, Colors::TabGhostFill, 0.f, st.borderWidth, Colors::TabGhostBorder);
        if (m_headerShown) heading(dragged, floatX, w);
        buf.popClip();
        buf.pushClip(b.x, b.y + hh, innerW, innerH - hh);
        JVectorCanvas ghost;
        for (int v = first; v <= last; ++v) {
            const int r = m_view[size_t(v)];
            const float y = b.y + hh + float(v) * rh - m_scrollY;
            content(ghost, r, dragged, JRect { floatX, y, w, rh }, m_selected.count(r) != 0);
        }
        ghost.flush(buf);
        buf.popClip();
    }
    buf.pushClip(b.x, b.y + hh, innerW, innerH - hh);

    // The cell being edited: its text, the chosen part and the caret.
    if (m_editing) {
        const int v = viewIndexOf(m_editRow);
        if (v >= 0) {
            const JRect cell = cellRect(v, m_editColumn);
            buf.pushRectangle(cell.x, cell.y, cell.width, cell.height, Colors::Surface1, 0.f, st.borderWidth * 2, Colors::Accent);
            const std::string& t = m_edit.text();
            // Text wider than the cell scrolls along it, the caret always inside (as a text field does).
            const float room = std::max(1.f, cell.width - 2 * pad);
            const float caretX = JTextHelper::measureWidth(t.substr(0, m_edit.caret()));
            const float fullW = JTextHelper::measureWidth(t);
            if (caretX - m_editScroll > room) m_editScroll = caretX - room;
            if (caretX < m_editScroll) m_editScroll = caretX;
            m_editScroll = std::max(0.f, std::min(m_editScroll, std::max(0.f, fullW - room)));
            const float tx = cell.x + pad - m_editScroll, ty = cell.y + (rh - lh) * 0.5f;
            buf.pushClip(cell.x + st.borderWidth, cell.y, std::max(0.f, cell.width - 2 * st.borderWidth), cell.height);
            if (m_edit.hasSelection()) {
                const float x0 = tx + JTextHelper::measureWidth(t.substr(0, m_edit.selectionStart()));
                const float x1 = tx + JTextHelper::measureWidth(t.substr(0, m_edit.selectionEnd()));
                uint8_t sel[4] = { Colors::Accent[0], Colors::Accent[1], Colors::Accent[2], 120 };
                buf.pushRectangle(x0, ty, std::max(1.f, x1 - x0), lh, sel);
            }
            JTextHelper::pushText(buf, tx, ty, t, Colors::ControlText, fullW + 1);
            buf.pushRectangle(tx + caretX, ty, st.borderWidth, lh, Colors::ControlText);
            buf.popClip();
        }
    }
    // Where a dragged row goes.
    if (m_dropBefore >= 0) {
        const float y = b.y + hh + float(m_dropBefore) * rh - m_scrollY;
        buf.pushRectangle(b.x, y - st.borderWidth, innerW, st.borderWidth * 2, Colors::Accent);
    }
    buf.popClip();

    // Scroll bars.
    const float contentH = float(m_view.size()) * rh, viewH = innerH - hh;
    if (contentH > viewH) {
        const float track = viewH, thumb = std::max(sb * 2, track * viewH / contentH);
        const float y = b.y + hh + (track - thumb) * (m_scrollY / std::max(1.f, contentH - viewH));
        buf.pushRectangle(b.x + innerW, b.y + hh, sb, track, Colors::Surface2);
        buf.pushRectangle(b.x + innerW, y, sb, thumb, Colors::Surface3, sb * 0.5f);
    }
}

// ---- mouse ------------------------------------------------------------------

void JPTable::clickHeader(int c) {
    // As Swing's DefaultRowSorter.toggleSortOrder.
    auto it = std::find_if(m_sortKeys.begin(), m_sortKeys.end(), [c](const SortKey& k) { return k.column == c; });
    if (it == m_sortKeys.end()) {
        m_sortKeys.insert(m_sortKeys.begin(), { c, true });
    } else if (it == m_sortKeys.begin()) {
        it->ascending = !it->ascending;
    } else {
        m_sortKeys.erase(it);
        m_sortKeys.insert(m_sortKeys.begin(), { c, true });
    }
    if (m_sortKeys.size() > kMostSortKeys) m_sortKeys.resize(kMostSortKeys);
    rebuildView();
    keepState();
}

void JPTable::selectView(int v, bool shift, bool ctrl) {
    if (v < 0 || size_t(v) >= m_view.size()) return;
    const int r = m_view[size_t(v)];
    if (shift && m_anchor >= 0 && viewIndexOf(m_anchor) >= 0) {
        const int a = viewIndexOf(m_anchor);
        if (!ctrl) m_selected.clear();
        for (int i = std::min(a, v); i <= std::max(a, v); ++i) m_selected.insert(m_view[size_t(i)]);
    } else if (ctrl) {
        if (!m_selected.erase(r)) m_selected.insert(r);
        m_anchor = r;
    } else {
        m_selected = { r };
        m_anchor = r;
    }
    m_lead = r;
    ensureVisible(v);
    selectionChanged();
}

void JPTable::handleMousePress(float mx, float my) {
    if (!m_model || !isPointInside(mx, my)) return;
    onClicked.emit();
    const JRect b = bounds();
    const float sb = JStyle::current().scrollBarWidth;
    if (mx >= b.x + b.width - sb && my >= b.y + headerHeight()) {
        m_draggingV = true;
        m_dragFromY = my;
        m_dragFromScroll = m_scrollY;
        return;
    }
    if (const int d = dividerAt(mx, my); d >= 0) {
        stopEditing(true);
        m_resizing = d;
        m_resizeFromX = mx;
        m_resizeFromW = columnWidth(d);
        return;
    }
    if (my < b.y + headerHeight()) {
        stopEditing(true);
        // A click sorts it, on letting go; moved, the heading is dragged instead.
        if (const int c = columnAt(mx); c >= 0) {
            m_headerPress = c;
            m_headerPressX = mx;
            m_grabOffset = mx - (b.x + columnX(c) - m_scrollX);
        }
        return;
    }
    const int v = viewRowAt(my);
    const int c = columnAt(mx);
    if (v < 0) {
        stopEditing(true);
        return;
    }
    const int r = m_view[size_t(v)];
    if (m_editing) {
        if (r == m_editRow && c == m_editColumn) {
            // A click in the cell being edited places the caret.
            const JRect cell = cellRect(v, c);
            const std::string& t = m_edit.text();
            size_t best = 0;
            const float left = cell.x + JStyle::current().gridCellPadding - m_editScroll;
            float bestD = std::fabs(mx - left);
            for (size_t i = 0; i < t.size();) {
                i = m_edit.nextCharStart(i);
                const float d = std::fabs(mx - (left + JTextHelper::measureWidth(t.substr(0, i))));
                if (d < bestD) {
                    bestD = d;
                    best = i;
                }
            }
            m_edit.setCaret(best, JWidget::s_shiftDown);
            m_graph.invalidateNode(m_nodeId, DirtySelf);
            return;
        }
        stopEditing(true);
    }
    if (c >= 0) m_leadColumn = c;
    // A press on a row of several chosen keeps them chosen: a tick or a choice goes into them all; a plain
    // click lets go of the others as it is released; a double-click edits them all (the rows chosen before its
    // first click put back).
    const bool plain = !JWidget::s_shiftDown && !JWidget::s_ctrlDown;
    if (plain && JWidget::s_doubleClick && m_beforeCollapse.size() > 1 && m_beforeCollapse.count(r)) m_selected = m_beforeCollapse;
    m_beforeCollapse.clear();
    m_collapseOnRelease = -1;
    if (plain && m_selected.size() > 1 && m_selected.count(r)) {
        m_lead = m_anchor = r;
        const JPTableModel::Kind k = c >= 0 ? m_model->column(c).kind : JPTableModel::Kind::Text;
        if (!JWidget::s_doubleClick && (k == JPTableModel::Kind::Text || k == JPTableModel::Kind::Number || !m_model->editable(r, c)))
            m_collapseOnRelease = r;
        ensureVisible(v);
        m_graph.invalidateNode(m_nodeId, DirtySelf);
    } else {
        selectView(v, JWidget::s_shiftDown, JWidget::s_ctrlDown);
    }
    if (canDragRows() && !JWidget::s_shiftDown && !JWidget::s_ctrlDown) {
        m_rowDrag = r;
        m_rowDragFromY = my;
    }
    if (c < 0 || JWidget::s_shiftDown || JWidget::s_ctrlDown) return;
    const JPTableModel::Kind kind = m_model->column(c).kind;
    const bool editable = m_model->editable(r, c);
    if (kind == JPTableModel::Kind::Boolean && editable) {
        toggle(r, c);
    } else if (kind == JPTableModel::Kind::Choice && editable) {
        openChoices(r, c);
    } else if (kind == JPTableModel::Kind::Picker && editable) {
        m_model->pick(r, c);
    } else if (JWidget::s_doubleClick) {
        if (editable && kind != JPTableModel::Kind::Boolean) startEditing(r, c, nullptr);
        else onRowActivated.emit(r);
    }
}

void JPTable::previewDrop(float mx) {
    // Its place: among the others, where its middle is.
    const JRect b = bounds();
    const int d = m_headerPress;
    std::vector<int> others;
    for (const int c : order())
        if (c != d) others.push_back(c);
    const float middle = mx - m_grabOffset + columnWidth(d) * 0.5f;
    float x = b.x - m_scrollX;
    size_t at = others.size();
    for (size_t i = 0; i < others.size(); ++i) {
        const float w = columnWidth(others[i]);
        if (middle < x + w * 0.5f) {
            at = i;
            break;
        }
        x += w;
    }
    others.insert(others.begin() + long(at), d);
    m_order = std::move(others);
}

void JPTable::endColumnDrag(bool keep) {
    if (!m_columnDrag) {
        m_headerPress = -1;
        return;
    }
    if (!keep) m_order = m_orderBefore;
    const bool moved = m_order != m_orderBefore;
    m_columnDrag = false;
    m_headerPress = -1;
    m_graph.invalidateNode(m_nodeId, DirtySelf);
    if (keep && moved) keepState();
}

bool JPTable::canDragRows() const {
    return m_model && m_model->reorderable() && m_sortKeys.empty() && !m_filter && int(m_view.size()) == m_model->rowCount();
}

void JPTable::handleMouseRelease(float mx, float my) {
    // A plain click on a row of several chosen, nothing else done: it alone chosen now (the others kept for a
    // double-click's second press).
    if (m_collapseOnRelease >= 0 && m_dropBefore < 0) {
        m_beforeCollapse = m_selected;
        m_selected = { m_collapseOnRelease };
        selectionChanged();
    }
    m_collapseOnRelease = -1;
    if (m_headerPress >= 0) {
        if (m_columnDrag) endColumnDrag(true);
        else {
            const int c = m_headerPress;
            m_headerPress = -1;
            clickHeader(c);
        }
    }
    if (m_resizing >= 0) keepState();
    m_resizing = -1;
    m_draggingV = false;
    const int from = m_rowDrag, before = m_dropBefore;
    m_rowDrag = m_dropBefore = -1;
    if (from >= 0 && before >= 0) {
        m_graph.invalidateNode(m_nodeId, DirtySelf);
        if (before != from && before != from + 1) {
            m_model->reorder(from, before);
            refresh();
            selectRow(before > from ? before - 1 : before);
        }
    }
    JControl::handleMouseRelease(mx, my);
}

void JPTable::handleMouseMove(float mx, float my) {
    if (m_headerPress >= 0) {
        if (!m_columnDrag && std::fabs(mx - m_headerPressX) > JStyle::current().doubleClickSlop) {
            m_columnDrag = true;
            m_orderBefore = order();
        }
        if (m_columnDrag) {
            m_dragX = mx;
            previewDrop(mx);
            m_graph.invalidateNode(m_nodeId, DirtySelf);
        }
        return;
    }
    if (m_resizing >= 0) {
        JWidget::s_hoverCursor = JPlatformCursor::ResizeLeftRight;
        setColumnWidth(m_resizing, m_resizeFromW + (mx - m_resizeFromX));
        return;
    }
    if (m_rowDrag >= 0) {
        // Moved half a row: the drag is on, the line where it goes shown.
        if (m_dropBefore >= 0 || std::fabs(my - m_rowDragFromY) > rowHeight() * 0.5f) {
            const JRect b = bounds();
            const float at = (my - (b.y + headerHeight()) + m_scrollY) / rowHeight();
            m_dropBefore = std::clamp(int(std::floor(at + 0.5f)), 0, int(m_view.size()));
            m_graph.invalidateNode(m_nodeId, DirtySelf);
        }
        return;
    }
    if (m_draggingV) {
        const JRect b = bounds();
        const float viewH = b.height - headerHeight();
        const float contentH = float(m_view.size()) * rowHeight();
        if (contentH > viewH) {
            m_scrollY = m_dragFromScroll + (my - m_dragFromY) * contentH / viewH;
            clampScroll();
            m_graph.invalidateNode(m_nodeId, DirtySelf);
        }
        return;
    }
    if (dividerAt(mx, my) >= 0) JWidget::s_hoverCursor = JPlatformCursor::ResizeLeftRight;
    // A heading's tooltip over its heading; a cell's (if it has one) over the rows.
    const JRect b = bounds();
    std::string tip;
    if (m_model && my >= b.y && my < b.y + headerHeight()) {
        if (const int c = columnAt(mx); c >= 0) tip = m_model->column(c).tooltip;
    } else if (m_model) {
        const int r = rowAt(my), c = columnAt(mx);
        if (r >= 0 && c >= 0) tip = m_model->cellTooltip(r, c);
        // Found by the search in a column not shown: which.
        if (const auto hit = m_hiddenHits.find(r); tip.empty() && hit != m_hiddenHits.end())
            tip = "Found in " + hit->second + ", a column not shown here (Columns… shows it)";
    }
    setTooltip(tip);
    JControl::handleMouseMove(mx, my);
}

bool JPTable::handleScroll(float mx, float my, float wheel) {
    if (!isPointInside(mx, my)) return false;
    m_scrollY -= wheel * rowHeight() * 3;
    clampScroll();
    m_graph.invalidateNode(m_nodeId, DirtySelf);
    return true;
}

void JPTable::fieldAdded(const std::string& key, const std::string& after) {
    m_addedHere.push_back({ key, after });
    m_orderKeys.clear();   // the columns made again, the field among them
    m_widths.clear();
    fieldsChanged();
}

void JPTable::buildHeaderMenu(int column) {
    // Kept until the next is made: the one open is this one.
    m_headerMenu = std::make_unique<JMenu>("Columns");
    const JPField* field = m_fieldsModel && column >= 0 ? m_fieldsModel->fieldAt(column) : nullptr;
    const std::optional<JPFieldSet::Of> of = m_fieldsModel && m_hasTools && m_offersFields ? m_fieldsModel->fieldsOf() : std::nullopt;
    if (m_model && column >= 0) {
        const JPTableModel::Column col = m_model->column(column);
        const std::string key = JPTableModel::keyOf(col);
        const std::string name = col.name.empty() ? col.tooltip.substr(0, col.tooltip.find(':')) : col.name;
        JMenuItem* hide = m_headerMenu->add(m_graph, "Hide \"" + name + "\"");
        hide->setTooltip("Not shown in this table (Columns… shows it again)");
        hide->setEnabled(order().size() > 1);
        hide->onTriggered.connect([this, key] { setColumnShown(key, false); });
        if (m_hasTools) {
            JMenuItem* everywhere = m_headerMenu->add(m_graph, "Show in Every View");
            everywhere->setTooltip("Shown in each of this table's views, not only this one");
            everywhere->setEnabled(m_views.size() > 1);
            everywhere->onTriggered.connect([this, key] { showInEveryView(key); });
        }
        if (of) {
            m_headerMenu->addSeparator(m_graph);
            JMenuItem* add = m_headerMenu->add(m_graph, "Add Field After \"" + name + "\"…");
            add->setTooltip(std::string("A field of your own for ") + JPFieldSet::plural(*of) + ", shown after this column");
            add->setEnabled(bool(s_fieldHooks.add));
            add->onTriggered.connect([this, of, key] {
                if (s_fieldHooks.add) s_fieldHooks.add(*of, std::string(), [this, key](const std::string& added) { fieldAdded(added, key); });
            });
            if (field) {
                JMenuItem* edit = m_headerMenu->add(m_graph, "Edit Field \"" + field->name + "\"…");
                edit->setTooltip(field->builtIn ? "Rename it, or change the headings an import matches it by"
                                                : "Rename it, change its type, its choices or the headings an import matches "
                                                  "it by, or delete it");
                edit->setEnabled(bool(s_fieldHooks.edit));
                edit->onTriggered.connect([of, fk = field->key] {
                    if (s_fieldHooks.edit) s_fieldHooks.edit(*of, fk);
                });
            }
        }
        m_headerMenu->addSeparator(m_graph);
    }
    JMenuItem* reset = m_headerMenu->add(m_graph, "Reset Columns");
    reset->setTooltip("Put this table's columns back as they first were: their order, their widths, no sort");
    reset->onTriggered.connect([this] { resetColumns(); });
}

void JPTable::prepareContextMenu(float mx, float my) {
    // Over the headings, the columns' menu (for the column under the pointer); over the rows, the owner's.
    if (contextMenu() != m_headerMenu.get()) m_rowsMenu = contextMenu();
    const JRect b = bounds();
    if (m_headerShown && my >= b.y && my < b.y + headerHeight()) {
        buildHeaderMenu(columnAt(mx));
        setContextMenu(m_headerMenu.get());
        return;
    }
    setContextMenu(m_rowsMenu);
    if (onContextMenu) onContextMenu(rowAt(my));
    (void)mx;
}

void JPTable::onFocusEvent(bool focused) {
    if (focused) s_focusedTable = this;
    if (!focused) {
        stopEditing(true);
        endColumnDrag(false);
    }
    m_graph.invalidateNode(m_nodeId, DirtySelf);
}

// ---- keys -------------------------------------------------------------------

void JPTable::moveLead(int v, bool extend) {
    if (m_view.empty()) return;
    v = std::clamp(v, 0, int(m_view.size()) - 1);
    selectView(v, extend, false);
}

bool JPTable::handleKeyEvent(const JKeyEvent& ke) {
    if (!ke.pressed || !m_model) return false;
    using K = JKeyEvent::JKey;
    if (m_columnDrag) {
        // Escape puts every column back; the drag goes on to the end otherwise.
        if (ke.key == K::Escape) endColumnDrag(false);
        return true;
    }
    if (m_editing) {
        if (ke.key == K::Escape) {
            stopEditing(false);
            return true;
        }
        if (ke.key == K::Tab || ke.key == K::BackTab) {
            const bool back = ke.key == K::BackTab || ke.shift;
            const int row = m_editRow, column = m_editColumn;
            if (!stopEditing(true, true)) return true;
            // On to the next cell that can be edited (Shift: the one before), along the row and on to the next
            // row's, as a spreadsheet goes: a text or number cell opened with its contents chosen, ready to type
            // over; another kind (a choice, a tick box) chosen, for F2 or Space.
            // Along the columns as they are shown.
            const std::vector<int> shown = order();
            const int columns = int(shown.size()), rows = int(m_view.size());
            int v = viewIndexOf(row), pos = positionOf(column);
            for (int steps = 0; steps < columns * std::max(1, rows) && v >= 0; ++steps) {
                pos += back ? -1 : 1;
                if (pos >= columns) { pos = 0; ++v; }
                if (pos < 0) { pos = columns - 1; --v; }
                if (v < 0 || v >= rows) break;
                const int c = shown[size_t(pos)];
                const int r = m_view[size_t(v)];
                if (!m_model->editable(r, c)) continue;
                m_lead = r;
                m_leadColumn = c;
                m_selected = { r };
                selectionChanged();
                const JPTableModel::Kind kind = m_model->column(c).kind;
                if (kind == JPTableModel::Kind::Text || kind == JPTableModel::Kind::Number) startEditing(r, c, nullptr);
                else ensureVisible(v);
                break;
            }
            m_graph.invalidateNode(m_nodeId, DirtySelf);
            return true;
        }
        const JTextEditCore::KeyResult res = m_edit.handleKey(ke);
        if (res.returnPressed) {
            stopEditing(true, true);
            return true;
        }
        if (res.consumed) m_graph.invalidateNode(m_nodeId, DirtySelf);
        return res.consumed;
    }
    if (onKey && onKey(ke)) return true;
    const int lead = viewIndexOf(m_lead);
    const int page = std::max(1, int((bounds().height - headerHeight()) / rowHeight()) - 1);
    switch (ke.key) {
        case K::Up:       moveLead(lead < 0 ? 0 : lead - 1, ke.shift); return true;
        case K::Down:     moveLead(lead < 0 ? 0 : lead + 1, ke.shift); return true;
        case K::PageUp:   moveLead(lead - page, ke.shift); return true;
        case K::PageDown: moveLead(lead + page, ke.shift); return true;
        case K::Home:     if (ke.ctrl) { moveLead(0, ke.shift); return true; } break;
        case K::End:      if (ke.ctrl) { moveLead(int(m_view.size()) - 1, ke.shift); return true; } break;
        case K::Left:
        case K::Right: {
            // The next column as shown.
            const std::vector<int>& shown = order();
            const int pos = std::clamp(positionOf(m_leadColumn) + (ke.key == K::Left ? -1 : 1), 0, int(shown.size()) - 1);
            if (pos >= 0) m_leadColumn = shown[size_t(pos)];
            m_graph.invalidateNode(m_nodeId, DirtySelf);
            return true;
        }
        default: break;
    }
    if (ke.ctrl && ke.key == K::A) {
        m_selected.clear();
        for (const int r : m_view) m_selected.insert(r);
        selectionChanged();
        return true;
    }
    if (ke.ctrl && ke.key == K::C) {
        copySelection();
        return true;
    }
    if (ke.ctrl && ke.key == K::V) {
        paste();
        return true;
    }
    if (m_lead < 0) return false;
    const JPTableModel::Kind kind = m_model->column(m_leadColumn).kind;
    const bool editable = m_model->editable(m_lead, m_leadColumn);
    if (ke.key == K::F2 && editable) {
        if (kind == JPTableModel::Kind::Choice) openChoices(m_lead, m_leadColumn);
        else if (kind == JPTableModel::Kind::Picker) m_model->pick(m_lead, m_leadColumn);
        else if (kind == JPTableModel::Kind::Boolean) toggle(m_lead, m_leadColumn);
        else startEditing(m_lead, m_leadColumn, nullptr);
        return true;
    }
    if (ke.key == K::Space && kind == JPTableModel::Kind::Boolean && editable) {
        toggle(m_lead, m_leadColumn);
        return true;
    }
    // Typing on a text cell starts editing it with what was typed.
    if (!ke.ctrl && !ke.alt && ke.utf8[0] && static_cast<unsigned char>(ke.utf8[0]) >= 0x20 && editable
        && (kind == JPTableModel::Kind::Text || kind == JPTableModel::Kind::Number)) {
        const std::string typed = ke.utf8;
        startEditing(m_lead, m_leadColumn, &typed);
        return true;
    }
    return false;
}

void JPTable::copySelection() const {
    std::string out;
    for (const int r : selectedRows()) {
        bool firstCell = true;
        for (const int c : order()) {   // as shown
            if (!firstCell) out += '\t';
            firstCell = false;
            out += m_model->column(c).kind == JPTableModel::Kind::Boolean ? (m_model->checked(r, c) ? "true" : "false")
                                                                          : m_model->text(r, c);
        }
        out += '\n';
    }
    JWidget::clipboardSet(out);
}

// ---- editing ----------------------------------------------------------------

namespace {

std::string folded(std::string s) {
    for (char& c : s) c = char(std::tolower(uint8_t(c)));
    return s;
}

} // namespace

std::string JPTable::cellValue(int row, int c) const {
    if (m_model->column(c).kind == JPTableModel::Kind::Boolean) return m_model->checked(row, c) ? "true" : "false";
    return m_model->text(row, c);
}

bool JPTable::setCellValue(int row, int c, const std::string& value, std::string& error) {
    switch (m_model->column(c).kind) {
        case JPTableModel::Kind::Boolean: {
            const std::string v = folded(value);
            bool on;
            if (v == "true" || v == "yes" || v == "y" || v == "1" || v == "x" || v == "on") on = true;
            else if (v == "false" || v == "no" || v == "n" || v == "0" || v == "off" || v.empty()) on = false;
            else {
                error = "'" + value + "' is not a tick (yes or no)";
                return false;
            }
            if (m_model->checked(row, c) != on) m_model->setChecked(row, c, on);
            return m_model->checked(row, c) == on || (error = "not changed", false);
        }
        case JPTableModel::Kind::Choice: {
            const std::vector<std::string> choices = m_model->choices(row, c);
            for (size_t i = 0; i < choices.size(); ++i)
                if (folded(choices[i]) == folded(value)) {
                    if (m_model->text(row, c) != choices[i]) m_model->setChoice(row, c, int(i));
                    return true;
                }
            error = "'" + value + "' is not one of its choices";
            return false;
        }
        case JPTableModel::Kind::Picker:
            error = "chosen in its own window";
            return false;
        default:
            if (m_model->text(row, c) == value) return true;
            if (!m_model->setText(row, c, value, error)) {
                if (error.empty()) error = "'" + value + "' is not taken";
                return false;
            }
            return true;
    }
}

std::vector<int> JPTable::editRows(int row) const {
    if (m_selected.size() <= 1 || !m_selected.count(row)) return { row };
    return selectedRows();
}

int JPTable::rowByKey(const std::string& key) const {
    for (int r = 0; m_model && r < m_model->rowCount(); ++r)
        if (m_model->rowKey(r) == key) return r;
    return -1;
}

int JPTable::columnByKey(const std::string& key) const {
    for (int c = 0; m_model && c < m_model->columnCount(); ++c)
        if (JPTableModel::keyOf(m_model->column(c)) == key) return c;
    return -1;
}

void JPTable::toggle(int row, int c) {
    // A tick, in every row chosen with it.
    std::vector<CellSet> sets;
    const std::string to = m_model->checked(row, c) ? "false" : "true";
    for (const int r : editRows(row))
        if (m_model->editable(r, c)) sets.push_back({ r, c, to });
    std::vector<std::string> refused;
    int changed = 0;
    setCells(sets, false, refused, changed);
}

bool JPTable::setCells(const std::vector<CellSet>& sets, bool atomic, std::vector<std::string>& refused, int& changed) {
    changed = 0;
    if (!m_model || sets.empty()) return true;
    std::vector<JPTableHistory::Cell> done;
    std::vector<std::pair<int, int>> where;   // each done cell's row and column, to put back
    m_model->beginEdits();
    for (const CellSet& set : sets) {
        if (set.row < 0 || set.row >= m_model->rowCount() || set.column < 0 || set.column >= m_model->columnCount()) continue;
        const std::string before = cellValue(set.row, set.column);
        if (before == set.value) continue;
        std::string error;
        if (!setCellValue(set.row, set.column, set.value, error)) {
            // Which row, by the first thing it shows.
            std::string row;
            for (const int c : order())
                if (row.empty()) row = m_model->text(set.row, c);
            refused.push_back(m_model->column(set.column).name + (row.empty() ? std::string() : " of " + row) + ": " + error);
            if (atomic) break;
            continue;
        }
        done.push_back({ m_model->rowKey(set.row), JPTableModel::keyOf(m_model->column(set.column)), before, cellValue(set.row, set.column) });
        where.push_back({ set.row, set.column });
    }
    if (atomic && !refused.empty()) {
        // Put back what was set, in the reverse order.
        for (size_t i = done.size(); i-- > 0;) {
            std::string ignored;
            setCellValue(where[i].first, where[i].second, done[i].before, ignored);
        }
        done.clear();
    }
    m_model->endEdits();
    refresh();
    changed = int(done.size());
    if (done.empty()) return refused.empty();
    // One step, named for it; said when it is more than one row.
    const std::optional<JPFieldSet::Of> of = m_model->fieldsOf();
    std::set<std::string> rows, columns;
    for (const auto& d : done) {
        rows.insert(d.row);
        columns.insert(d.column);
    }
    const std::string kind = of ? JPFieldSet::plural(*of) : std::string("Rows");
    const int c = columnByKey(*columns.begin());
    std::string text = columns.size() == 1 ? "Change " + (c >= 0 ? m_model->column(c).name : std::string("a Cell")) : "Change " + std::to_string(done.size()) + " Cells";
    if (rows.size() > 1) text += " of " + std::to_string(rows.size()) + " " + kind;
    // Those a history of the model's own notes (the library's) are not stepped back here as well.
    std::vector<JPTableHistory::Cell> ours;
    for (size_t i = 0; i < done.size(); ++i)
        if (!m_model->ownHistoryAt(where[i].second)) ours.push_back(done[i]);
    if (s_history) s_history->record(*this, ours, text);
    const bool undone = s_history || std::any_of(where.begin(), where.end(), [this](const auto& w) { return m_model->ownHistoryAt(w.second); });
    if (rows.size() > 1 && s_status) s_status(text + (undone ? " (Edit \xE2\x96\xB8 Undo takes it back)" : ""));
    return refused.empty();
}

void JPTable::putCells(const std::vector<JPTableHistory::Cell>& cells, bool before) {
    if (!m_model) return;
    stopEditing(false);
    m_model->beginEdits();
    for (const JPTableHistory::Cell& cell : cells) {
        const int r = rowByKey(cell.row), c = columnByKey(cell.column);
        if (r < 0 || c < 0) continue;
        std::string ignored;
        setCellValue(r, c, before ? cell.before : cell.after, ignored);
    }
    m_model->endEdits();
    refresh();
}

void JPTable::paste() {
    if (!m_model || m_lead < 0) return;
    stopEditing(true);
    // The clipboard's rows and cells (a tab between them, as Ctrl+C copies them).
    std::vector<std::vector<std::string>> block;
    std::string text = JWidget::clipboardGet();
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
    if (text.empty()) return;
    size_t from = 0;
    while (from <= text.size()) {
        size_t nl = text.find('\n', from);
        std::string line = text.substr(from, nl == std::string::npos ? std::string::npos : nl - from);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::vector<std::string> cells;
        size_t at = 0;
        while (true) {
            const size_t tab = line.find('\t', at);
            cells.push_back(line.substr(at, tab == std::string::npos ? std::string::npos : tab - at));
            if (tab == std::string::npos) break;
            at = tab + 1;
        }
        block.push_back(std::move(cells));
        if (nl == std::string::npos) break;
        from = nl + 1;
    }
    const std::vector<int>& shown = order();
    const int startPos = std::max(0, positionOf(m_leadColumn));
    std::vector<CellSet> sets;
    int passed = 0;
    auto put = [&](int r, int pos, const std::string& v) {
        if (pos < 0 || size_t(pos) >= shown.size()) return;
        const int c = shown[size_t(pos)];
        if (!m_model->editable(r, c) || m_model->column(c).kind == JPTableModel::Kind::Picker) {
            ++passed;
            return;
        }
        sets.push_back({ r, c, v });
    };
    if (block.size() == 1 && block[0].size() == 1 && m_selected.size() > 1) {
        // One value: into every row chosen.
        for (const int r : selectedRows()) put(r, startPos, block[0][0]);
    } else {
        // A block: down from the row chosen, along from its column, as the table shows them.
        const int v0 = viewIndexOf(m_lead);
        for (size_t i = 0; i < block.size(); ++i) {
            const int v = v0 + int(i);
            if (v < 0 || size_t(v) >= m_view.size()) break;
            for (size_t j = 0; j < block[i].size(); ++j) put(m_view[size_t(v)], startPos + int(j), block[i][j]);
        }
    }
    std::vector<std::string> refused;
    int changed = 0;
    if (!setCells(sets, true, refused, changed)) {
        std::string why = "Nothing pasted: ";
        for (size_t i = 0; i < refused.size() && i < 3; ++i) why += (i ? "; " : "") + refused[i];
        if (onEditRefused) onEditRefused(why);
        if (s_status) s_status(why);
        return;
    }
    if (s_status && (changed > 0 || passed > 0))
        s_status(std::to_string(changed) + " cell(s) pasted" + (passed ? ", " + std::to_string(passed) + " that cannot be changed passed over" : std::string()));
}

void JPTable::startEditing(int r, int c, const std::string* typed) {
    stopEditing(true);
    m_editing = true;
    m_editRow = r;
    m_editColumn = c;
    m_leadColumn = c;
    m_editScroll = 0;
    if (typed) {
        m_edit.setText(*typed);
    } else {
        m_edit.setText(m_model->text(r, c));
        m_edit.selectAll();   // as OpenPnP's AutoSelectTextTable
    }
    ensureVisible(viewIndexOf(r));
    m_graph.invalidateNode(m_nodeId, DirtySelf);
}

bool JPTable::stopEditing(bool keep, bool stayIfRefused) {
    if (!m_editing) return true;
    const int r = m_editRow, c = m_editColumn;
    if (keep && m_model && r < m_model->rowCount() && m_edit.text() != m_model->text(r, c)) {
        // Into every row chosen with it (those that can take it); refused by one, by none.
        std::vector<CellSet> sets;
        for (const int row : editRows(r))
            if (m_model->editable(row, c)) sets.push_back({ row, c, m_edit.text() });
        std::vector<std::string> refused;
        int changed = 0;
        const std::string typed = m_edit.text();
        m_editing = false;   // the rows made again as they change: not drawn as edited meanwhile
        const bool taken = setCells(sets, true, refused, changed);
        m_editing = true;
        if (!taken) {
            const std::string error = refused.empty() ? std::string() : refused.front().substr(refused.front().find(": ") + 2);
            m_edit.setText(typed);
            if (onEditRefused && !error.empty()) onEditRefused(error);
            // As a Swing table: a value it cannot take keeps the cell open.
            if (stayIfRefused) {
                m_graph.invalidateNode(m_nodeId, DirtySelf);
                return false;
            }
        }
        m_editing = false;
        m_editRow = m_editColumn = -1;
        refresh();
        return true;
    }
    m_editing = false;
    m_editRow = m_editColumn = -1;
    m_graph.invalidateNode(m_nodeId, DirtySelf);
    return true;
}

void JPTable::openChoices(int r, int c) {
    if (!JComboBox::onOpenPopupHook) return;
    const std::vector<std::string> choices = m_model->choices(r, c);
    if (choices.empty()) return;
    const std::string now = m_model->text(r, c);
    // Its list opens under the cell, as wide as its longest choice (never narrower than the cell, nor wider
    // than the table).
    JRect cell = cellRect(viewIndexOf(r), c);
    const JStyle& st = JStyle::current();
    float widest = 0;
    for (const std::string& choice : choices) widest = std::max(widest, JTextHelper::measureWidth(choice));
    const JRect b = bounds();
    cell.width = std::min(std::max(cell.width, widest + 2 * st.spacing + st.scrollBarWidth), b.width);
    cell.x = std::min(cell.x, b.x + b.width - cell.width);
    m_choiceCombo = std::make_unique<JComboBox>(m_graph, choices, cell.width, cell.height);
    m_choiceCombo->setBounds(cell);
    for (size_t i = 0; i < choices.size(); ++i)
        if (choices[i] == now) m_choiceCombo->setCurrentIndex(int(i));
    if (auto search = m_model->choiceSearch(r, c)) m_choiceCombo->setSearch(std::move(*search));
    m_choiceCombo->onIndexChanged.connect([this, r, c, choices](int index) {
        if (!m_model || r >= m_model->rowCount() || index < 0 || size_t(index) >= choices.size()) return;
        // The choice, in every row chosen with it.
        std::vector<CellSet> sets;
        for (const int row : editRows(r))
            if (m_model->editable(row, c)) sets.push_back({ row, c, choices[size_t(index)] });
        std::vector<std::string> refused;
        int changed = 0;
        setCells(sets, false, refused, changed);
    });
    JComboBox::onOpenPopupHook(m_choiceCombo.get());
}

} // inline namespace jf
