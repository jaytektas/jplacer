// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPMachineSetupPanel.h"

#include "JPUiParts.h"

#include "common/JPlacerLog.h"
#include "setup/JPSetupEdits.h"
#include "setup/JPSetupProperties.h"
#include "setup/JPSetupTree.h"

#include <j/core/Log.h>

#include <algorithm>

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

std::string joined(const std::vector<std::string>& parts) {
    std::string out;
    for (const std::string& p : parts) out += (out.empty() ? "" : "/") + p;
    return out;
}

} // namespace

JPMachineSetupPanel::JPMachineSetupPanel(JSceneGraph& graph, JPCellConfig cell, std::vector<std::string> profiles,
                                         std::string selected)
    : JContainer(graph), m_original(cell), m_draft(std::move(cell)), m_profiles(std::move(profiles)) {
    JPUiParts::asPanel(*this);

    auto tools = JPUiParts::row(graph);
    m_add = tools->add(JPUiParts::button(graph, "Add"));
    m_add->onClicked.connect([this] {
        const std::string added = JPSetupEdits::add(m_draft, m_selected);
        if (added.empty()) return;
        JLOGC(JPlacerLog::kUi, JLogLevel::Info) << "Machine Setup: added " << added;
        m_note->setText("");
        rebuildTree();
        select(added);
    });
    m_remove = tools->add(JPUiParts::button(graph, "Remove"));
    m_remove->onClicked.connect([this] {
        const std::string group = JPSetupTree::groupOf(m_draft, m_selected);
        std::string why;
        if (!JPSetupEdits::remove(m_draft, m_selected, why)) {
            m_note->setText("Not removed: " + why + ".");
            return;
        }
        JLOGC(JPlacerLog::kUi, JLogLevel::Info) << "Machine Setup: removed " << m_selected;
        m_note->setText("");
        rebuildTree();
        select(group);
    });
    m_up = tools->add(JPUiParts::button(graph, "Up"));
    m_up->onClicked.connect([this] { moveSelected(-1); });
    m_down = tools->add(JPUiParts::button(graph, "Down"));
    m_down->onClicked.connect([this] { moveSelected(+1); });
    JLineEdit* search = tools->add(std::make_unique<JLineEdit>(graph, "Search"));
    search->setHSizePolicy(JSizePolicyMode::Expanding, 1);
    add(std::move(tools));

    m_tree = add(std::make_unique<JTreeView>(graph, 0.f, 0.f));   // sized by its share (below)
    m_tree->setVSizePolicy(JSizePolicyMode::Expanding, 1);
    search->onTextChanged.connect([this](const std::string& text) { m_tree->setFilter(text); });
    m_tree->onSelectionChanged.connect([this](JTreeViewNode* n) {
        if (n && n->userData != m_selected) show(n->userData);
    });

    m_title = add(std::make_unique<JLabel>(graph, ""));
    m_scroll = add(std::make_unique<JScrollArea>(graph, 0.f, 0.f));
    m_scroll->setVSizePolicy(JSizePolicyMode::Expanding, 2);
    m_form = m_scroll->addChildWidget(std::make_unique<JPPropertyForm>(graph));
    m_form->onChanged = [this](const std::string& property) { changed(property); };

    m_problems = add(std::make_unique<JLabel>(graph, ""));
    m_problems->setWordWrap(true);
    auto bottom = JPUiParts::row(graph);
    m_note = bottom->add(std::make_unique<JLabel>(graph, ""));
    m_note->setHSizePolicy(JSizePolicyMode::Expanding, 1);
    m_reset = bottom->add(JPUiParts::button(graph, "Reset"));
    m_reset->onClicked.connect([this] {
        m_draft = m_original;
        m_note->setText("");
        rebuildTree();
        const std::string keep = m_selected;
        m_selected.clear();
        select(keep);
    });
    m_apply = bottom->add(JPUiParts::button(graph, "Apply"));
    m_apply->onClicked.connect([this] {
        if (onApply) onApply(m_draft);
    });
    add(std::move(bottom));

    rebuildTree();
    select(selected.empty() ? "machine" : selected);
}

void JPMachineSetupPanel::moveSelected(int by) {
    const std::string moved = JPSetupEdits::move(m_draft, m_selected, by);
    if (!moved.empty()) {
        rebuildTree();
        m_selected.clear();
        select(moved);   // a step's path is its place
    }
    update();
}

void JPMachineSetupPanel::collectExpanded(const JTreeViewNode& n) {
    if (n.expanded && !n.userData.empty()) m_expanded.insert(n.userData);
    for (const JTreeViewNode& c : n.children) collectExpanded(c);
}

void JPMachineSetupPanel::rebuildTree() {
    const bool firstTime = m_tree->root().children.empty();
    m_expanded.clear();
    collectExpanded(m_tree->root());
    JTreeViewNode top;
    top.expanded = true;
    top.children.push_back(rows(JPSetupTree::build(m_draft), m_expanded, firstTime));
    m_tree->setRootNode(std::move(top));
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
    m_form->setModel(std::move(f.model));
    if (onSelected) onSelected(path);
    update();
}

void JPMachineSetupPanel::changed(const std::string& property) {
    // A name shows in the tree; a head moves a part in it.
    rebuildTree();
    if (std::find(m_reshaping.begin(), m_reshaping.end(), property) != m_reshaping.end()) {
        JPSetupProperties::Form f = JPSetupProperties::forNode(m_draft, m_selected, m_profiles);
        m_reshaping = f.reshaping;
        m_title->setText(f.title);
        m_form->setModel(std::move(f.model));
        select(m_selected);   // where it is in the tree now
    }
    update();
}

void JPMachineSetupPanel::update() {
    const std::string what = JPSetupEdits::addable(m_draft, m_selected);
    m_add->setLabel(what.empty() ? "Add" : "Add " + what);
    m_add->setEnabled(!what.empty());
    const std::string kind = JPSetupTree::parse(m_selected).kind;
    // A step of unloading that is loading backwards is shown, not changed.
    const bool part = kind == "step" ? !what.empty() : kind != "machine" && kind != "group";
    m_remove->setEnabled(part);
    m_up->setEnabled(part);
    m_down->setEnabled(part);

    std::string problems;
    for (const std::string& p : m_draft.problems()) problems += (problems.empty() ? "" : "\n") + p;
    m_problems->setText(problems.empty() ? "" : "To put right before applying:\n" + problems);
    const bool edited = m_draft.toJson().dump() != m_original.toJson().dump();
    m_reset->setEnabled(edited);
    m_apply->setEnabled(edited && problems.empty());
}

} // inline namespace jf
