// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPMachineSetupPanel.h"

#include "JPIcons.h"
#include "JPUiParts.h"

#include "common/JPlacerLog.h"
#include "setup/JPSetupEdits.h"
#include "setup/JPSetupProperties.h"
#include "setup/JPSetupTree.h"

#include <j/core/JStyle.h>
#include <j/core/Log.h>

#include <algorithm>
#include <functional>

inline namespace jf {

namespace {

// The tree view's rows from Machine Setup's tree; a node is open if it was
// (by path), and the machine and its groups start open.
JTreeViewNode rows(const JPSetupTree::Node& n, const std::set<std::string>& expanded, bool firstTime) {
    JTreeViewNode r;
    r.label = n.label;
    r.userData = n.path;
    r.expanded = expanded.count(n.path) > 0 || (firstTime && (n.path == "machine" || n.path.rfind("group:", 0) == 0));
    for (const JPSetupTree::Node& c : n.children) r.children.push_back(rows(c, expanded, firstTime));
    return r;
}

// What the node at `path` is called in the tree.
std::string nameOf(const JPCellConfig& cell, const std::string& path) {
    const std::vector<std::string> labels = JPSetupTree::labelsTo(JPSetupTree::build(cell), path);
    return labels.empty() ? path : labels.back();
}

std::string joined(const std::vector<std::string>& parts) {
    std::string out;
    for (const std::string& p : parts) out += (out.empty() ? "" : "/") + p;
    return out;
}

} // namespace

JPMachineSetupPanel::JPMachineSetupPanel(JSceneGraph& graph, JPCellConfig cell, std::vector<std::string> profiles,
                                         std::string selected, double treeShare)
    : JContainer(graph), m_inUse(cell), m_draft(cell), m_recorded(std::move(cell)),
      m_history([this](const JPSetupHistory::State& state) { restore(state); }), m_profiles(std::move(profiles)) {
    JPUiParts::asPanel(*this);
    m_history.onChanged = [this] {
        update();
        if (onHistory) onHistory();
    };
    m_settle.setInterval(kSettleMs).setSingleShot(true);
    m_settle.timeout.connect([this] { settle(); });

    auto tools = JPUiParts::row(graph);
    m_add = tools->add(JPUiParts::button(graph, "Add"));
    m_add->onClicked.connect([this] { addPart(); });
    m_remove = tools->add(JPUiParts::button(graph, "Remove"));
    m_remove->onClicked.connect([this] { removePart(); });
    m_up = tools->add(JPUiParts::button(graph, "Up"));
    m_up->onClicked.connect([this] { moveSelected(-1); });
    m_down = tools->add(JPUiParts::button(graph, "Down"));
    m_down->onClicked.connect([this] { moveSelected(+1); });
    add(std::move(tools));

    // The tree over the selected part's settings, a divider between them to drag.
    m_treePane = std::make_unique<JContainer>(graph, 0.f, 0.f);
    m_formPane = std::make_unique<JContainer>(graph, 0.f, 0.f);
    for (JContainer* pane : { m_treePane.get(), m_formPane.get() })
        pane->setDirection(JFlexDirection::Column)->setGap(2 * JStyle::current().spacing)->setAlignItems(JAlignItems::Stretch)
            ->setShrinkStretchyFirst(true);
    m_split = add(std::make_unique<JSplitter>(graph, JSplitter::JOrientation::Vertical, 0.f, 0.f));
    m_split->setHostsPanes(true);
    m_split->setVSizePolicy(JSizePolicyMode::Expanding, 1);
    m_split->addPane(m_treePane.get(), float(treeShare));
    m_split->addPane(m_formPane.get(), float(1 - treeShare));

    // Over the tree: open every branch, close them all, and a filter (its ✕ clears it).
    auto find = JPUiParts::row(graph);
    m_expandAll = find->add(std::make_unique<JPIconButton>(graph, "Open All", &JPIcons::expandAll, "Open every branch"));
    m_collapseAll = find->add(std::make_unique<JPIconButton>(graph, "Close All", &JPIcons::collapseAll, "Close every branch"));
    m_expandAll->onClicked.connect([this] { m_tree->expandAll(); });
    m_collapseAll->onClicked.connect([this] { collapseAll(); });
    JLineEdit* search = m_search = find->add(std::make_unique<JLineEdit>(graph, "Search"));
    search->setHSizePolicy(JSizePolicyMode::Expanding, 1);
    search->setClearButtonEnabled(true);
    m_treePane->add(std::move(find));

    m_tree = m_treePane->add(std::make_unique<JTreeView>(graph, 0.f, 0.f));   // the rest of its pane
    m_tree->setVSizePolicy(JSizePolicyMode::Expanding, 1);
    search->onTextChanged.connect([this](const std::string& text) { m_tree->setFilter(text); });
    m_tree->onSelectionChanged.connect([this](JTreeViewNode* n) {
        if (n && n->userData != m_selected) show(n->userData);
    });
    // Right-click a row (it is selected first): its branch, the whole tree, and Add / Remove.
    m_treeMenu = std::make_unique<JMenu>("Machine Setup");
    m_treeMenu->add(graph, "Open This Branch")->onTriggered.connect([this] { setBranch(true); });
    m_treeMenu->add(graph, "Close This Branch")->onTriggered.connect([this] { setBranch(false); });
    m_treeMenu->addSeparator(graph);
    m_treeMenu->add(graph, "Open All")->onTriggered.connect([this] { m_tree->expandAll(); });
    m_treeMenu->add(graph, "Close All")->onTriggered.connect([this] { collapseAll(); });
    m_treeMenu->addSeparator(graph);
    m_menuAdd = m_treeMenu->add(graph, "Add");
    m_menuAdd->onTriggered.connect([this] { addPart(); });
    m_menuRemove = m_treeMenu->add(graph, "Remove");
    m_menuRemove->onTriggered.connect([this] { removePart(); });
    m_tree->setContextMenu(m_treeMenu.get());
    m_tree->setRightClickSelects(true);

    m_title = m_formPane->add(std::make_unique<JLabel>(graph, ""));
    m_scroll = m_formPane->add(std::make_unique<JScrollArea>(graph, 0.f, 0.f));
    m_scroll->setVSizePolicy(JSizePolicyMode::Expanding, 1);
    m_form = m_scroll->addChildWidget(std::make_unique<JPPropertyForm>(graph));
    m_form->onChanged = [this](const std::string& property) { changed(property); };

    m_problems = add(std::make_unique<JLabel>(graph, ""));
    m_problems->setWordWrap(true);
    auto bottom = JPUiParts::row(graph);
    m_note = bottom->add(std::make_unique<JLabel>(graph, ""));
    m_note->setHSizePolicy(JSizePolicyMode::Expanding, 1);
    m_undo = bottom->add(JPUiParts::button(graph, "Undo"));
    m_undo->onClicked.connect([this] { undo(); });
    m_redo = bottom->add(JPUiParts::button(graph, "Redo"));
    m_redo->onClicked.connect([this] { redo(); });
    add(std::move(bottom));

    rebuildTree();
    select(selected.empty() ? "machine" : selected);
}

double JPMachineSetupPanel::treeShare() const {
    const std::vector<float> f = m_split->fractions();
    return f.empty() ? kTreeShare : f.front();
}

void JPMachineSetupPanel::moveSelected(int by) {
    const std::string from = m_selected, name = nameOf(m_draft, from);
    const std::string moved = JPSetupEdits::move(m_draft, m_selected, by);
    if (moved.empty()) return;
    rebuildTree();
    m_selected.clear();
    select(moved);   // a step's path is its place
    record((by < 0 ? "Move Up " : "Move Down ") + name, "", from);
}

void JPMachineSetupPanel::change(const std::string& what, const std::function<void(JPCellConfig&)>& edit) {
    const std::string from = m_selected;
    edit(m_draft);
    rebuildTree();
    m_form->refresh();
    record(what, "", from);
    m_settle.stop();
    settle();
}

void JPMachineSetupPanel::record(const std::string& what, const std::string& key, const std::string& from) {
    JPSetupHistory::State after{ m_draft, m_selected };
    m_history.record(what, key, { std::move(m_recorded), from }, after);
    m_recorded = std::move(after.cell);
    m_settle.start();
}

void JPMachineSetupPanel::restore(const JPSetupHistory::State& state) {
    m_draft = state.cell;
    m_recorded = state.cell;
    m_note->setText("");
    rebuildTree();
    m_selected.clear();
    select(state.selected);
    m_settle.start();
}

void JPMachineSetupPanel::settle() {
    if (!m_draft.problems().empty() || m_draft.toJson().dump() == m_inUse.toJson().dump()) return;
    if (!onApply) return;
    if (onApply(m_draft)) m_inUse = m_draft;
    else m_settle.start();   // not taken now (the machine is moving): again shortly
}

std::string JPMachineSetupPanel::undoLabel() const {
    return canUndo() ? "Undo " + m_history.undoText() : "Undo";
}

std::string JPMachineSetupPanel::redoLabel() const {
    return canRedo() ? "Redo " + m_history.redoText() : "Redo";
}

void JPMachineSetupPanel::undo() {
    m_history.undo();
}

void JPMachineSetupPanel::redo() {
    m_history.redo();
}

void JPMachineSetupPanel::showNode(const std::string& path) {
    if (!m_search->text().empty()) {
        m_search->setText("");
        m_tree->setFilter("");
    }
    select(path);
}

void JPMachineSetupPanel::collectExpanded(const JTreeViewNode& n) {
    if (n.expanded && !n.userData.empty()) m_expanded.insert(n.userData);
    for (const JTreeViewNode& c : n.children) collectExpanded(c);
}

void JPMachineSetupPanel::rebuildTree() {
    const bool firstTime = m_tree->root().children.empty();
    m_expanded.clear();
    collectExpanded(m_tree->root());
    setRows(firstTime);
}

void JPMachineSetupPanel::setRows(bool firstTime) {
    JTreeViewNode top;
    top.expanded = true;
    top.children.push_back(rows(JPSetupTree::build(m_draft), m_expanded, firstTime));
    m_tree->setRootNode(std::move(top));
}

void JPMachineSetupPanel::addPart() {
    const std::string from = m_selected, what = JPSetupEdits::addable(m_draft, m_selected);
    const std::string added = JPSetupEdits::add(m_draft, m_selected);
    if (added.empty()) return;
    JLOGC(JPlacerLog::kUi, JLogLevel::Info) << "Machine Setup: added " << added;
    m_note->setText("");
    rebuildTree();
    select(added);
    record("Add " + what, "", from);
}

void JPMachineSetupPanel::removePart() {
    const std::string group = JPSetupTree::groupOf(m_draft, m_selected);
    const std::string from = m_selected, name = nameOf(m_draft, from);
    std::string why;
    if (!JPSetupEdits::remove(m_draft, m_selected, why)) {
        m_note->setText("Not removed: " + why + ".");
        return;
    }
    JLOGC(JPlacerLog::kUi, JLogLevel::Info) << "Machine Setup: removed " << m_selected;
    m_note->setText("");
    rebuildTree();
    select(group);
    record("Remove " + name, "", from);
}

void JPMachineSetupPanel::setBranch(bool open) {
    m_expanded.clear();
    collectExpanded(m_tree->root());
    // The selected node and everything under it.
    std::function<bool(const JPSetupTree::Node&, bool)> walk = [&](const JPSetupTree::Node& n, bool inside) {
        inside = inside || n.path == m_selected;
        if (inside) {
            if (open) m_expanded.insert(n.path);
            else m_expanded.erase(n.path);
        }
        bool found = inside;
        for (const JPSetupTree::Node& c : n.children) found = walk(c, inside) || found;
        return found;
    };
    walk(JPSetupTree::build(m_draft), false);
    setRows(false);
    const std::string keep = m_selected;
    m_selected.clear();
    select(keep);
}

void JPMachineSetupPanel::collapseAll() {
    // Down to the machine's groups: closing the machine too would leave one row.
    m_expanded = { "machine" };
    setRows(false);
    m_selected.clear();
    select("machine");
}

void JPMachineSetupPanel::select(const std::string& path) {
    const std::vector<std::string> labels = JPSetupTree::labelsTo(JPSetupTree::build(m_draft), path);
    if (labels.empty()) {
        if (path != "machine") select("machine");   // gone (another cell, or removed elsewhere)
        return;
    }
    // Selecting in the tree shows it (onSelectionChanged), unless it is shown already.
    const std::string before = m_selected;
    m_tree->selectByPath(joined(labels));
    if (m_selected == before && before != path) show(path);
}

void JPMachineSetupPanel::show(const std::string& path) {
    m_selected = path;
    JPSetupProperties::Form f = JPSetupProperties::forNode(m_draft, path, m_profiles);
    m_reshaping = f.reshaping;
    m_title->setText(f.title);
    m_labels.clear();
    for (const JProperty& p : f.model.all()) m_labels[p.name] = p.meta.label.empty() ? p.name : p.meta.label;
    m_form->setModel(std::move(f.model));
    if (onSelected) onSelected(path);
    update();
}

void JPMachineSetupPanel::changed(const std::string& property) {
    const auto label = m_labels.find(property);
    const std::string what = nameOf(m_draft, m_selected) + ": " + (label == m_labels.end() ? property : label->second);
    const std::string at = m_selected;
    // A name shows in the tree; a head moves a part in it.
    rebuildTree();
    if (std::find(m_reshaping.begin(), m_reshaping.end(), property) != m_reshaping.end()) {
        JPSetupProperties::Form f = JPSetupProperties::forNode(m_draft, m_selected, m_profiles);
        m_reshaping = f.reshaping;
        m_title->setText(f.title);
        m_form->setModel(std::move(f.model));
        select(m_selected);   // where it is in the tree now
    }
    // Typing on in the same field is the same step.
    record(what, at + "|" + property, at);
}

void JPMachineSetupPanel::update() {
    const std::string what = JPSetupEdits::addable(m_draft, m_selected);
    m_add->setLabel(what.empty() ? "Add" : "Add " + what);
    m_add->setEnabled(!what.empty());
    m_menuAdd->setLabel(m_add->label());
    m_menuAdd->setEnabled(!what.empty());
    const std::string kind = JPSetupTree::parse(m_selected).kind;
    // A step of unloading that is loading backwards is shown, not changed.
    const bool part = kind == "step" ? !what.empty() : kind != "machine" && kind != "group";
    m_remove->setEnabled(part);
    m_menuRemove->setEnabled(part);
    m_up->setEnabled(part);
    m_down->setEnabled(part);

    std::string problems;
    for (const std::string& p : m_draft.problems()) problems += (problems.empty() ? "" : "\n") + p;
    m_problems->setText(problems.empty() ? "" : "Not in use until put right:\n" + problems);
    m_undo->setEnabled(canUndo());
    m_undo->setTooltip(undoLabel());
    m_redo->setEnabled(canRedo());
    m_redo->setTooltip(redoLabel());
}

} // inline namespace jf
