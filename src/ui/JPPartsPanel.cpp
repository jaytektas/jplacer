// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPPartsPanel.h"

#include "JPUiParts.h"

#include <algorithm>
#include <cctype>
#include <map>

inline namespace jf {

namespace {

std::string lowerCase(const std::string& s) {
    std::string out;
    for (const char c : s) out += char(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// A tree row's label: its key, then the cells the grouping does not already say.
std::string rowLabel(const JPPartsPanel::Row& row, int group) {
    std::string label = row.key;
    for (size_t c = 1; c < row.cells.size(); ++c) {
        if (int(c) == group || row.cells[c].empty()) continue;
        label += "   " + row.cells[c];
    }
    return label;
}

void selectedKeys(const JTreeViewNode& n, std::vector<std::string>& out) {
    if (n.selected && !n.userData.empty()) out.push_back(n.userData);
    // A group chosen stands for every row in it.
    for (const JTreeViewNode& c : n.children)
        if (n.selected && n.userData.empty()) out.push_back(c.userData);
        else selectedKeys(c, out);
}

} // namespace

JPPartsPanel::JPPartsPanel(JSceneGraph& graph, std::vector<std::string> columns, std::string noun, std::vector<Page> pages)
    : JContainer(graph), m_columns(std::move(columns)), m_noun(std::move(noun)), m_pages(std::move(pages)) {
    JPUiParts::asPanel(*this);

    auto top = JPUiParts::row(graph);
    m_filterEdit = top->add(std::make_unique<JLineEdit>(graph, "Filter"));
    m_filterEdit->setHSizePolicy(JSizePolicyMode::Expanding, 1);
    m_filterEdit->onTextChanged.connect([this](const std::string& text) {
        m_filter = lowerCase(text);
        refill();
    });
    m_viewChoice = top->add(std::make_unique<JPChoiceRow>(graph, std::vector<std::string>{ "List", "Tree" }, 0));
    m_viewChoice->onChosen.connect([this](int i) {
        if (m_updating) return;
        m_view = i == 1 ? View::Tree : View::List;
        refill();
        if (onViewChanged) onViewChanged(m_view, m_group);
    });
    m_groupLabel = top->add(std::make_unique<JLabel>(graph, "Group by"));
    m_groupBy = top->add(std::make_unique<JComboBox>(graph, std::vector<std::string>(m_columns.begin() + 1, m_columns.end())));
    m_groupBy->onIndexChanged.connect([this](int i) {
        if (m_updating || i < 0) return;
        m_group = i + 1;   // the key column is not a grouping
        refill();
        if (onViewChanged) onViewChanged(m_view, m_group);
    });
    add(std::move(top));

    m_stack = add(std::make_unique<JStackedWidget>(graph));
    m_stack->setVSizePolicy(JSizePolicyMode::Expanding, 2);
    m_stack->setHSizePolicy(JSizePolicyMode::Expanding, 1);
    m_listOwned = std::make_unique<JDataGrid>(graph, m_columns);
    m_list = m_listOwned.get();
    m_stack->addWidget(m_list);
    m_list->setSortable(true);
    m_list->setColumnsResizable(true);
    m_list->setSelectionMode(JDataGrid::SelectionMode::Extended);
    m_list->onSelectionChanged.connect([this](int) {
        if (m_updating) return;
        std::vector<std::string> keys;
        for (const int i : m_list->selectedIndices())
            if (i >= 0 && size_t(i) < m_shown.size()) keys.push_back(m_rows[m_shown[size_t(i)]].key);
        chosen(std::move(keys));
    });
    m_list->onRowActivated.connect([this](int i) {
        if (onActivated && i >= 0 && size_t(i) < m_shown.size()) onActivated(m_rows[m_shown[size_t(i)]].key);
    });

    m_treeOwned = std::make_unique<JTreeView>(graph);
    m_tree = m_treeOwned.get();
    m_stack->addWidget(m_tree);
    m_tree->setMultiSelect(true);
    m_tree->onSelectionChanged.connect([this](JTreeViewNode*) {
        if (m_updating) return;
        std::vector<std::string> keys;
        selectedKeys(m_tree->root(), keys);
        chosen(std::move(keys));
    });
    m_tree->onNodeActivated.connect([this](JTreeViewNode* n) {
        if (onActivated && n && !n->userData.empty()) onActivated(n->userData);
    });

    m_count = add(std::make_unique<JLabel>(graph, ""));
    if (!m_pages.empty()) {
        m_tabs = add(std::make_unique<JTabWidget>(graph, 0.f, 0.f));
        m_tabs->setVSizePolicy(JSizePolicyMode::Expanding, 3);
        for (Page& p : m_pages) m_tabs->addTab(p.title, p.page.get());
    }
    setView(View::List, 1);
}

JPPartsPanel::~JPPartsPanel() {
    // The stack and the tabs hold what this owns: they let go of it first.
    m_stack->removeWidget(m_list);
    m_stack->removeWidget(m_tree);
    if (m_tabs)
        for (size_t i = 0; i < m_pages.size(); ++i) m_tabs->removeTab(0);
}

void JPPartsPanel::setView(View view, int groupColumn) {
    m_view = view;
    m_group = std::clamp(groupColumn, 1, std::max(1, int(m_columns.size()) - 1));
    m_updating = true;
    m_viewChoice->choose(view == View::Tree ? 1 : 0);
    m_groupBy->setCurrentIndex(m_group - 1);
    m_updating = false;
    refill();
}

void JPPartsPanel::showRows(std::vector<Row> rows) {
    m_rows = std::move(rows);
    refill();
}

void JPPartsPanel::showPage(size_t i) {
    if (m_tabs && i < m_pages.size()) m_tabs->setActiveTab(int(i));
}

std::vector<std::string> JPPartsPanel::chosenKeys() const {
    return m_chosen;
}

bool JPPartsPanel::matches(const Row& row) const {
    if (m_filter.empty()) return true;
    if (lowerCase(row.key).find(m_filter) != std::string::npos) return true;
    return std::any_of(row.cells.begin(), row.cells.end(),
                       [this](const std::string& c) { return lowerCase(c).find(m_filter) != std::string::npos; });
}

void JPPartsPanel::refill() {
    const bool tree = m_view == View::Tree;
    m_stack->setCurrentWidget(tree ? static_cast<JWidget*>(m_tree) : m_list);
    // Grouping is the tree's: there, and greyed for the list.
    m_groupLabel->setEnabled(tree);
    m_groupBy->setEnabled(tree);
    m_updating = true;
    if (tree) fillTree();
    else fillList();
    m_updating = false;
    size_t shown = 0;
    for (const Row& r : m_rows) shown += matches(r) ? 1 : 0;
    m_count->setText(shown == m_rows.size() ? std::to_string(m_rows.size()) + " " + m_noun
                                            : std::to_string(shown) + " of " + std::to_string(m_rows.size()) + " " + m_noun);
}

void JPPartsPanel::fillList() {
    // The chosen rows stay chosen across a refill, by key.
    std::vector<std::string> keep;
    for (const int i : m_list->selectedIndices())
        if (i >= 0 && size_t(i) < m_shown.size()) keep.push_back(m_rows[m_shown[size_t(i)]].key);
    m_shown.clear();
    std::vector<std::vector<std::string>> cells;
    for (size_t i = 0; i < m_rows.size(); ++i) {
        if (!matches(m_rows[i])) continue;
        m_shown.push_back(i);
        cells.push_back(m_rows[i].cells);
    }
    m_list->setRows(cells);
    m_list->clearSelection();
    if (!keep.empty())
        for (size_t v = 0; v < m_shown.size(); ++v)
            if (std::find(keep.begin(), keep.end(), m_rows[m_shown[v]].key) != keep.end()) {
                m_list->setSelectedIndex(int(v));
                break;
            }
}

void JPPartsPanel::fillTree() {
    // Groups in the order of their names; a group open before stays open.
    std::map<std::string, bool> open;
    for (const JTreeViewNode& g : m_tree->root().children) open[g.label.substr(0, g.label.rfind("  ("))] = g.expanded;
    std::map<std::string, std::vector<const Row*>> groups;
    for (const Row& r : m_rows)
        if (matches(r)) groups[size_t(m_group) < r.cells.size() ? r.cells[size_t(m_group)] : std::string()].push_back(&r);
    JTreeViewNode root;
    root.expanded = true;
    for (const auto& [name, rows] : groups) {
        JTreeViewNode g;
        const std::string title = name.empty() ? "(none)" : name;
        g.label = title + "  (" + std::to_string(rows.size()) + ")";
        g.expanded = !m_filter.empty() || (open.count(title) && open[title]);
        for (const Row* r : rows) {
            JTreeViewNode n;
            n.label = rowLabel(*r, m_group);
            n.userData = r->key;
            g.children.push_back(std::move(n));
        }
        root.children.push_back(std::move(g));
    }
    m_tree->setRootNode(std::move(root));
}

void JPPartsPanel::chosen(std::vector<std::string> keys) {
    m_chosen = keys;
    if (onChosen) onChosen(keys);
}

} // inline namespace jf
