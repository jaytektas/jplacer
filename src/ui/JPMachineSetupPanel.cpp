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
#include <cstdlib>
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

JPMachineSetupPanel::JPMachineSetupPanel(JSceneGraph& graph, JPCellConfig cell, std::vector<JPFirmwareProfile> profiles,
                                         std::string selected, double treeShare)
    : JContainer(graph), m_inUse(cell), m_draft(cell), m_recorded(std::move(cell)),
      m_history([this](const JPSetupHistory::State& state) { restore(state); }), m_profiles(std::move(profiles)) {
    JPUiParts::asPanel(*this);
    m_history.onChanged = [this] {
        update();
        if (onHistory) onHistory();
    };
    m_retry.setInterval(kRetryMs).setSingleShot(true);
    m_retry.timeout.connect([this] { handOver(); });

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

    // The tree beside the selected part's settings, each the full height, a
    // divider between them to drag.
    m_treePane = std::make_unique<JContainer>(graph, 0.f, 0.f);
    m_formPane = std::make_unique<JContainer>(graph, 0.f, 0.f);
    for (JContainer* pane : { m_treePane.get(), m_formPane.get() })
        pane->setDirection(JFlexDirection::Column)->setGap(2 * JStyle::current().spacing)->setAlignItems(JAlignItems::Stretch)
            ->setShrinkStretchyFirst(true);
    m_split = add(std::make_unique<JSplitter>(graph, JSplitter::JOrientation::Horizontal, 0.f, 0.f));
    m_split->setHostsPanes(true);
    m_split->setVSizePolicy(JSizePolicyMode::Expanding, 1);
    m_split->addPane(m_treePane.get(), float(treeShare));
    m_split->addPane(m_formPane.get(), float(1 - treeShare));

    // Over the tree: a filter, its ✕ on the right clearing it. (Opening and
    // closing every branch is the tree's right-click menu.)
    auto find = JPUiParts::row(graph);
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
    m_form = m_formPane->add(std::make_unique<JPSetupForm>(graph));
    m_form->setVSizePolicy(JSizePolicyMode::Expanding, 1);
    m_form->onChanged = [this](const std::string& property) { changed(property); };
    m_form->onAction = [this](const std::string& action) {
        if (onAction) onAction(m_selected, action);
    };
    m_form->onCapture = [this](const JPSetupProperties::Row& row, JPSetupForm::Tool tool) { capture(row, tool); };
    m_form->onMoveTo = [this](const JPSetupProperties::Row& row, JPSetupForm::Tool tool) { goTo(row, tool); };

    m_problems = add(std::make_unique<JLabel>(graph, ""));
    m_problems->setWordWrap(true);
    showLine(m_problems, "");
    // What the last action could not do: a line only while there is one.
    // (Undo and Redo are Edit's, with their keys.)
    m_note = add(std::make_unique<JLabel>(graph, ""));
    m_note->setWordWrap(true);
    showLine(m_note, "");

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
}

void JPMachineSetupPanel::record(const std::string& what, const std::string& key, const std::string& from) {
    JPSetupHistory::State after{ m_draft, m_selected };
    m_history.record(what, key, { std::move(m_recorded), from }, after);
    m_recorded = std::move(after.cell);
    handOver();
}

void JPMachineSetupPanel::restore(const JPSetupHistory::State& state) {
    m_draft = state.cell;
    // What was measured is not undone: the calibrations and squareness in use.
    for (JPCameraConfig& c : m_draft.cameras)
        for (const JPCameraConfig& u : m_inUse.cameras)
            if (u.id == c.id) c.calibrations = u.calibrations;
    m_draft.squareness = m_inUse.squareness;
    m_recorded = m_draft;
    setNote("");
    rebuildTree();
    m_selected.clear();
    select(state.selected);
    handOver();
}

void JPMachineSetupPanel::handOver() {
    m_retry.stop();
    if (!m_draft.problems().empty() || m_draft.toJson().dump() == m_inUse.toJson().dump()) return;
    if (!onApply) return;
    if (onApply(m_draft)) m_inUse = m_draft;
    else m_retry.start();   // not taken now (the machine is moving): again shortly
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
    setNote("");
    rebuildTree();
    select(added);
    record("Add " + what, "", from);
}

void JPMachineSetupPanel::removePart() {
    const std::string group = JPSetupTree::groupOf(m_draft, m_selected);
    const std::string from = m_selected, name = nameOf(m_draft, from);
    std::string why;
    if (!JPSetupEdits::remove(m_draft, m_selected, why)) {
        setNote("Not removed: " + why + ".");
        return;
    }
    JLOGC(JPlacerLog::kUi, JLogLevel::Info) << "Machine Setup: removed " << m_selected;
    setNote("");
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
    JPSetupProperties::Form f = JPSetupProperties::forNode(m_draft, path, m_profiles, m_config);
    m_reshaping = f.reshaping;
    m_title->setText(f.title);
    m_labels.clear();
    for (const JProperty& p : f.model.all()) m_labels[p.name] = p.meta.label.empty() ? p.name : p.meta.label;
    m_form->setForm(std::move(f));
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
        // The form is made again on the next frame: the change arrives inside
        // one of its controls' own events, and that control goes with it.
        remakeForm();
    }
    // Typing on in the same field is the same step.
    record(what, at + "|" + property, at);
}

void JPMachineSetupPanel::measured(const std::function<void(JPCellConfig&)>& edit) {
    edit(m_draft);
    edit(m_recorded);
    edit(m_inUse);
    remakeForm();
}

void JPMachineSetupPanel::remakeForm() {
    std::weak_ptr<bool> alive = m_alive;
    jPostToNextFrame([this, alive] {
        if (!alive.lock()) return;
        JPSetupProperties::Form f = JPSetupProperties::forNode(m_draft, m_selected, m_profiles, m_config);
        m_reshaping = f.reshaping;
        m_title->setText(f.title);
        m_labels.clear();
        for (const JProperty& p : f.model.all()) m_labels[p.name] = p.meta.label.empty() ? p.name : p.meta.label;
        m_form->setForm(std::move(f));
        select(m_selected);   // where it is in the tree now
    });
}

void JPMachineSetupPanel::capture(const JPSetupProperties::Row& row, JPSetupForm::Tool tool) {
    const std::string at = m_selected;
    bool any = false;
    if (row.place == JPSetupProperties::Place::Axis) {
        const std::optional<double> v = axisAt ? axisAt(row.axis) : std::nullopt;
        if (v && !row.cells.empty()) any = m_form->set(row.cells.front().property, JVariant(*v));
    } else {
        const Where now = whereIs ? whereIs(tool) : Where{};
        // The row's cells are X, Y, Z and rotation, in that order; an empty one is skipped.
        for (size_t i = 0; i < row.cells.size() && i < now.size(); ++i)
            if (!row.cells[i].property.empty() && now[i]) any = m_form->set(row.cells[i].property, JVariant(*now[i])) || any;
    }
    if (!any) {
        setNote("Nothing captured: the machine is not connected, or nothing is chosen to capture from.");
        return;
    }
    setNote("");
    rebuildTree();
    record("Capture " + nameOf(m_draft, at) + ": " + row.label, "", at);
}

void JPMachineSetupPanel::goTo(const JPSetupProperties::Row& row, JPSetupForm::Tool tool) {
    // A coordinate as a number, or empty (a step's coordinate left out).
    auto value = [this](const std::string& property) -> std::optional<double> {
        if (property.empty()) return std::nullopt;
        const JVariant v = m_form->get(property);
        if (v.isDouble() || v.isInt()) return v.toDouble();
        const std::string t = v.toString();
        char* end = nullptr;
        const double d = std::strtod(t.c_str(), &end);
        if (t.empty() || end == t.c_str()) return std::nullopt;
        return d;
    };
    if (row.place == JPSetupProperties::Place::Axis) {
        if (const auto v = row.cells.empty() ? std::nullopt : value(row.cells.front().property); v && moveAxis)
            moveAxis(row.axis, *v);
        return;
    }
    Where to;
    for (size_t i = 0; i < row.cells.size() && i < to.size(); ++i) to[i] = value(row.cells[i].property);
    if (tool == JPSetupForm::Tool::Camera) to[2].reset();   // a camera stays at safe Z
    if (moveTo) moveTo(tool, to);
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
    showLine(m_problems, problems.empty() ? "" : "Not in use until put right:\n" + problems);
}

void JPMachineSetupPanel::setNote(const std::string& text) {
    showLine(m_note, text);
}

void JPMachineSetupPanel::showLine(JLabel* line, const std::string& text) {
    // Hidden, it takes no room (the tree and the settings have it); shown, as
    // tall as its text folded to the panel's width.
    line->setText(text);
    line->setVisible(!text.empty());
    if (text.empty()) {
        line->setMinimumSize(0.f, 0.f);
        line->setMaximumSize(kNoLimit, 0.f);
    } else {
        const float width = m_graph.getLayoutConst(getNodeId()).boundingBox.width;
        line->setMaximumSize(kNoLimit, kNoLimit);
        line->setMinimumSize(0.f, line->heightFor(std::max(width, 1.f)));
    }
}

} // inline namespace jf
