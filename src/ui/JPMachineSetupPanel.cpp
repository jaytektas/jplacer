// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPMachineSetupPanel.h"

#include "JPIcons.h"
#include "JPUiParts.h"

#include "common/JPlacerLog.h"
#include "setup/JPSetupEdits.h"
#include "setup/JPSetupProperties.h"
#include "setup/JPSetupTree.h"

#include <j/core/JCheckBox.h>
#include <j/core/Dialog.h>
#include <j/core/JStyle.h>
#include <j/core/Log.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>

inline namespace jf {

namespace {

// The tree view's rows from Machine Setup's tree; a node is open if it was
// (by path), and the machine and its groups start open.
JTreeViewNode rows(const JPSetupTree::Node& n, const std::set<std::string>& expanded, bool firstTime) {
    JTreeViewNode r;
    r.label = n.label;
    r.userData = n.path;
    r.icon = JPSetupTreeView::iconOf(n.icon);
    r.expanded = expanded.count(n.path) > 0 || (firstTime && (n.path == "machine" || n.path.rfind("group:", 0) == 0));
    for (const JPSetupTree::Node& c : n.children) r.children.push_back(rows(c, expanded, firstTime));
    return r;
}

// What the node at `path` is called in the tree.
std::string nameOf(const JPCellConfig& cell, const std::string& path) {
    const std::vector<std::string> labels = JPSetupTree::labelsTo(JPSetupTree::build(cell), path);
    return labels.empty() ? path : labels.back();
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

    // OpenPnP's tools: the selected part's own (a nozzle tip's Unload and Load, Delete, Permutate Up and
    // Down), then its group's New (update() shows those the selection has, named and drawn for its kind).
    auto tools = JPUiParts::row(graph);
    auto tool = [&](const char* name, const char* icon) { return std::make_unique<JPIconButton>(graph, name, icon, ""); };
    m_unload = tool("Unload", "nozzletip-unload");
    m_unload->setTooltip("Unload the currently loaded nozzle tip.");
    m_unload->onClicked.connect([this] { if (onAction) onAction(m_selected, "unloadNozzleTip"); });
    m_load = tool("Load", "nozzletip-load");
    m_load->setTooltip("Load the currently selected nozzle tip.");
    m_load->onClicked.connect([this] { if (onAction) onAction(m_selected, "loadNozzleTip"); });
    m_remove = tool("Delete", "general-remove");
    m_remove->setLeads(JPIconButton::Leads::Elsewhere);   // asks first
    m_remove->onClicked.connect([this] { confirmRemove(); });
    m_up = tool("Permutate Up", "arrow-up");
    m_up->onClicked.connect([this] { moveSelected(-1); });
    m_down = tool("Permutate Down", "arrow-down");
    m_down->onClicked.connect([this] { moveSelected(+1); });
    m_add = tool("New", "general-add");
    m_add->onClicked.connect([this] { addPart(); });
    m_tools = add(std::move(tools));

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

    // Over the tree: OpenPnP's Expand (ticked, every branch open; not, every
    // one closed; the tree's right-click menu does either too), and a filter,
    // its ✕ on the right clearing it.
    auto find = JPUiParts::row(graph);
    JCheckBox* expand = find->add(std::make_unique<JCheckBox>(graph, "Expand", 0.f));
    expand->setHSizePolicy(JSizePolicyMode::Fixed);
    expand->setTooltip("Expand machine configuration tree");
    expand->onStateChanged.connect([this](bool on) {
        if (on) m_tree->expandAll();
        else collapseAll();
    });
    JLineEdit* search = m_search = find->add(std::make_unique<JLineEdit>(graph, "Search"));
    search->setHSizePolicy(JSizePolicyMode::Expanding, 1);
    search->setClearButtonEnabled(true);
    m_treePane->add(std::move(find));

    m_tree = m_treePane->add(std::make_unique<JPSetupTreeView>(graph));   // the rest of its pane
    m_tree->setVSizePolicy(JSizePolicyMode::Expanding, 1);
    search->onTextChanged.connect([this](const std::string& text) { m_tree->setFilter(text); });
    // The search finds a part by its name or by any setting on its page ("motion": the controller's Motion
    // Control Type, the machine's Motion Planner).
    m_tree->setFilterMatcher([this](const JTreeViewNode& n, const std::string& filter) {
        return !n.userData.empty() && wordsOf(n.userData).find(filter) != std::string::npos;
    });
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
    m_menuRemove->onTriggered.connect([this] { confirmRemove(); });
    m_tree->setContextMenu(m_treeMenu.get());
    m_tree->setRightClickSelects(true);

    m_title = m_formPane->add(std::make_unique<JLabel>(graph, ""));
    m_form = m_formPane->add(std::make_unique<JPSetupForm>(graph));
    m_form->setVSizePolicy(JSizePolicyMode::Expanding, 1);
    m_form->onChanged = [this](const std::string& property) { changed(property); };
    m_form->onAction = [this](const std::string& action) {
        // A feeder's page: the Feeders tab works its buttons.
        if (const std::string feeder = shownFeeder(); !feeder.empty()) {
            if (feederPages.act) feederPages.act(feeder, action);
            m_form->refresh();
            return;
        }
        // A change the button makes itself (a profile added): a step to undo, the form made again.
        if (const auto e = m_edits.find(action); e != m_edits.end()) {
            const std::string at = m_selected, what = nameOf(m_draft, at) + ": " + e->second.what;
            const auto apply = e->second.apply;
            apply();
            rebuildTree();
            remakeForm();
            record(what, "", at);
            return;
        }
        // The default vision settings' page's: as the Vision tab does them.
        if ((action.rfind("bottom:", 0) == 0 || action.rfind("fiducial:", 0) == 0) && !shownVisionSettings().empty()) {
            if (visionAction) visionAction(shownVisionSettings(), action);
            return;
        }
        if (action == "gcode:export" || action == "gcode:copy") {
            exportGcode(action == "gcode:copy");
            return;
        }
        if (onAction) onAction(m_selected, action);
    };
    m_form->onCapture = [this](const JPSetupProperties::Row& row, JPSetupForm::Tool tool) {
        if (const std::string feeder = shownFeeder(); !feeder.empty()) {
            if (feederPages.place) feederPages.place(feeder, row, tool, true, false);
            m_form->refresh();
            return;
        }
        capture(row, tool);
    };
    m_form->onMoveTo = [this](const JPSetupProperties::Row& row, JPSetupForm::Tool tool) {
        if (const std::string feeder = shownFeeder(); !feeder.empty()) {
            if (feederPages.place) feederPages.place(feeder, row, tool, false, false);
            return;
        }
        goTo(row, tool);
    };
    m_form->onMoveToStraight = [this](const JPSetupProperties::Row& row, JPSetupForm::Tool tool) {
        if (const std::string feeder = shownFeeder(); !feeder.empty()) {
            if (feederPages.place) feederPages.place(feeder, row, tool, false, true);
            return;
        }
        goTo(row, tool, true);
    };
    m_form->onContactProbe = [this](const JPSetupProperties::Row& row) { probe(row); };

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
    // A nozzle's offsets changed: what depends on them follows, in the same step (OpenPnP's).
    m_draft.followNozzleOffsets(m_recorded);
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

const std::string& JPMachineSetupPanel::wordsOf(const std::string& path) {
    if (const auto it = m_words.find(path); it != m_words.end()) return it->second;
    // The page as forNode makes it, nothing live attached (feeders: the Feeders tab's page).
    const JPSetupTree::Path p = JPSetupTree::parse(path);
    const JPSetupProperties::Form f = p.kind == "feeder"
        ? (feederPages.page ? feederPages.page(p.id) : JPSetupProperties::Form {})
        : JPSetupProperties::forNode(m_draft, path, m_profiles, m_config);
    std::string words;
    for (const JPSetupProperties::Tab& t : f.tabs) words += "\n" + JPSetupProperties::words(t);
    return m_words[path] = words;
}

void JPMachineSetupPanel::setRows(bool firstTime) {
    m_words.clear();   // the parts, or what is on their pages, may have changed
    JTreeViewNode top;
    top.expanded = true;
    top.children.push_back(rows(JPSetupTree::build(m_draft, m_config), m_expanded, firstTime));
    m_tree->setRootNode(std::move(top));
}

void JPMachineSetupPanel::addPart() {
    // A group of several kinds (Signalers): the kind chosen first, as OpenPnP's New Signaler….
    const std::vector<std::string> kinds = JPSetupEdits::kinds(m_draft, m_selected);
    if (!kinds.empty()) {
        if (!chooseClass) return;
        const std::string what = JPSetupEdits::addable(m_draft, m_selected);
        // OpenPnP's titles name a controller a Driver.
        // OpenPnP's words, its "implemention" for a nozzle too.
        const std::string named = what == "Controller" ? "Driver" : what;
        chooseClass("Select " + named + "...",
                    "Please select a" + std::string(named == "Actuator" ? "n " : " ") + named
                        + (named == "Nozzle" ? " implemention" : " implementation") + " from the list below.",
                    kinds,
                    [this, alive = std::weak_ptr<bool>(m_alive)](std::string kind) {
                        if (const auto a = alive.lock(); !a || !*a || kind.empty()) return;
                        addPart(kind);
                    });
        return;
    }
    addPart("");
}

void JPMachineSetupPanel::addPart(const std::string& kind) {
    const std::string from = m_selected, what = JPSetupEdits::addable(m_draft, m_selected);
    const std::string added = JPSetupEdits::add(m_draft, m_selected, kind);
    if (added.empty()) return;
    JLOGC(JPlacerLog::kUi, JLogLevel::Info) << "Machine Setup: added " << added;
    setNote("");
    rebuildTree();
    select(added);
    record("Add " + what, "", from);
}

void JPMachineSetupPanel::confirmRemove() {
    // As OpenPnP's: asked first (and one step to undo after).
    const std::string at = m_selected, name = nameOf(m_draft, at);
    JDialogOptions opts;
    opts.okLabel = "Yes";
    opts.cancelLabel = "No";
    JDialog::confirm("Delete " + name + "?", "Are you sure you want to delete " + name + "?",
                     [this, at, alive = std::weak_ptr<bool>(m_alive)] {
                         if (const auto a = alive.lock(); !a || !*a || m_selected != at) return;
                         removePart();
                     }, nullptr, opts);
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
    walk(JPSetupTree::build(m_draft, m_config), false);
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
    const std::vector<std::string> labels = JPSetupTree::labelsTo(JPSetupTree::build(m_draft, m_config), path);
    if (labels.empty()) {
        if (path != "machine") select("machine");   // gone (another cell, or removed elsewhere)
        return;
    }
    // Selecting in the tree shows it (onSelectionChanged), unless it is shown already.
    const std::string before = m_selected;
    m_tree->selectByLabels(labels);   // a name may hold a "/" ("0805 / 0603")
    if (m_selected == before && before != path) show(path);
}

JPSetupProperties::Form JPMachineSetupPanel::formFor(const std::string& path) {
    // A feeder's: the Feeders tab's page, with OpenPnP's place buttons as there.
    if (const JPSetupTree::Path p = JPSetupTree::parse(path); p.kind == "feeder") {
        m_configProperties.clear();
        m_form->setOpenPnpPlaceButtons(true);
        JPSetupProperties::Form f = feederPages.page ? feederPages.page(p.id) : JPSetupProperties::Form {};
        if (const JPFeeder* feeder = m_config ? m_config->feeder(p.id) : nullptr)
            f.title = feeder->typeName() + " " + (feeder->name().empty() ? feeder->id() : feeder->name());
        return f;
    }
    m_form->setOpenPnpPlaceButtons(false);
    JPSetupProperties::Form f = JPSetupProperties::forNode(m_draft, path, m_profiles, m_config, m_visionTests.angle ? &m_visionTests : nullptr,
                                                           m_motionTest ? &*m_motionTest : nullptr, live);
    m_configProperties.clear();
    for (const JPSetupProperties::Tab& t : f.tabs)
        if (t.title.size() > 15 && t.title.compare(t.title.size() - 15, 15, "Vision Settings") == 0)
            for (const JPSetupProperties::Group& g : t.groups)
                for (const JPSetupProperties::Row& r : g.rows)
                    for (const JPSetupProperties::Cell& c : r.cells)
                        if (!c.property.empty() && !c.button) m_configProperties.insert(c.property);
    return f;
}

std::string JPMachineSetupPanel::shownVisionSettings() const {
    const JPSetupTree::Path p = JPSetupTree::parse(m_selected);
    if (p.kind != "vision") return {};
    if (p.id == "bottom") return m_draft.vision.bottomVisionId;
    if (p.id == "fiducial") return m_draft.vision.fiducialVisionId;
    return {};
}

void JPMachineSetupPanel::refreshForm() { m_form->refresh(); }

void JPMachineSetupPanel::setConfiguration(JPConfiguration* config) {
    m_config = config;
    feedersChanged();
}

std::string JPMachineSetupPanel::shownFeeder() const {
    const JPSetupTree::Path p = JPSetupTree::parse(m_selected);
    return p.kind == "feeder" ? p.id : std::string();
}

void JPMachineSetupPanel::feedersChanged() {
    rebuildTree();
    if (!shownFeeder().empty()) select(m_selected);   // gone: the machine instead
}

void JPMachineSetupPanel::feederPageChanged(bool remade) {
    if (shownFeeder().empty()) return;
    if (remade) remakeForm();
    else m_form->refresh();
}

void JPMachineSetupPanel::show(const std::string& path) {
    m_selected = path;
    JPSetupProperties::Form f = formFor(path);
    m_reshaping = f.reshaping;
    m_viewOnly = f.viewOnly;
    m_edits = f.edits;
    m_title->setText(f.title);
    m_labels.clear();
    for (const JProperty& p : f.model.all()) m_labels[p.name] = p.meta.label.empty() ? p.name : p.meta.label;
    // Found by the search through a setting: on the first tab that holds it.
    std::string tab;
    if (std::string filter = m_search->text(); !filter.empty()) {
        for (char& c : filter) c = char(std::tolower(static_cast<unsigned char>(c)));
        for (const JPSetupProperties::Tab& t : f.tabs)
            if (JPSetupProperties::words(t).find(filter) != std::string::npos) {
                tab = t.title;
                break;
            }
    }
    m_form->setForm(std::move(f), tab);
    if (onSelected) onSelected(path);
    update();
}

void JPMachineSetupPanel::changed(const std::string& property) {
    // A feeder's page: the Feeders tab takes the change (and saves it); its name shows in the tree.
    if (const std::string feeder = shownFeeder(); !feeder.empty()) {
        if (feederPages.edited) feederPages.edited(feeder, property);
        rebuildTree();
        m_form->refresh();
        return;
    }
    // The default vision settings' (the configuration's): saved with it, not undone here.
    if (m_configProperties.count(property)) {
        if (property.find(":parameter:") != std::string::npos && visionAction) visionAction(shownVisionSettings(), property);
        if (onConfigurationChanged) onConfigurationChanged();
        return;
    }
    if (std::find(m_viewOnly.begin(), m_viewOnly.end(), property) != m_viewOnly.end()) {
        if (std::find(m_reshaping.begin(), m_reshaping.end(), property) != m_reshaping.end()) remakeForm();
        return;
    }
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
    m_form->refresh();   // what shows the same setting another way (a slider's number, a graph)
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
        JPSetupProperties::Form f = formFor(m_selected);
        m_reshaping = f.reshaping;
        m_viewOnly = f.viewOnly;
        m_edits = f.edits;
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
        Where now = whereIs ? whereIs(tool) : Where{};
        // A camera's Z is what the head's Z probe finds there (OpenPnP's capture), filled in once it has read.
        if (tool == JPSetupForm::Tool::Camera && probeZ && now[0] && now[1]) {
            const JPSetupProperties::Row r = row;
            if (probeZ(*now[0], *now[1], [this, r, now, at, alive = std::weak_ptr<bool>(m_alive)](double z) {
                    // Another part chosen meanwhile: what was captured is for a page no longer shown.
                    if (const auto a = alive.lock(); !a || !*a || m_selected != at) return;
                    Where probed = now;
                    probed[2] = z;
                    applyCapture(r, probed, at);
                }))
                return;
        }
        applyCapture(row, now, at);
        return;
    }
    if (!any) {
        setNote("Nothing captured: the machine is not connected, or nothing is chosen to capture from.");
        return;
    }
    setNote("");
    rebuildTree();
    record("Capture " + nameOf(m_draft, at) + ": " + row.label, "", at);
}

void JPMachineSetupPanel::applyCapture(const JPSetupProperties::Row& row, const Where& now, const std::string& at) {
    // The row's cells are X, Y, Z and rotation, in that order; an empty one is skipped.
    bool any = false;
    for (size_t i = 0; i < row.cells.size() && i < now.size(); ++i)
        if (!row.cells[i].property.empty() && now[i]) any = m_form->set(row.cells[i].property, JVariant(*now[i])) || any;
    if (!any) {
        setNote("Nothing captured: the machine is not connected, or nothing is chosen to capture from.");
        return;
    }
    setNote("");
    rebuildTree();
    record("Capture " + nameOf(m_draft, at) + ": " + row.label, "", at);
}

void JPMachineSetupPanel::goTo(const JPSetupProperties::Row& row, JPSetupForm::Tool tool, bool straight) {
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
    auto& go = straight ? moveToStraight : moveTo;
    if (go) go(tool, to);
}

void JPMachineSetupPanel::exportGcode(bool toClipboard) {
    // The controller shown, as it is set up here (OpenPnP writes its whole driver).
    const JPDriverConfig* d = m_draft.driver(JPSetupTree::parse(m_selected).id);
    if (!d) return;
    const std::string text = d->toJson().dump(2);
    if (toClipboard) {
        JWidget::clipboardSet(text);
        JDialog::message("Copied Gcode", "Copied Gcode to Clipboard");
        return;
    }
    JDialog::saveFile("Save Gcode Profile As...", { "json" }, [text](std::string path) {
        if (path.empty()) return;
        if (!path.ends_with(".json")) path += ".json";
        auto write = [text, path] {
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            out << text;
            if (!out) JDialog::message("Export Failed", "Could not write " + path + ".");
        };
        if (!std::filesystem::exists(path)) {
            write();
            return;
        }
        JDialog::confirm("Replace file?",
                         std::filesystem::path(path).filename().string() + " already exists. Do you want to replace it?", write);
    });
}

void JPMachineSetupPanel::probe(const JPSetupProperties::Row& row) {
    // Where the row says, its Z probed by the chosen contact probing nozzle, and that Z put in it.
    Where to;
    for (size_t i = 0; i < row.cells.size() && i < to.size(); ++i) {
        const JVariant v = m_form->get(row.cells[i].property);
        if (v.isDouble() || v.isInt()) to[i] = v.toDouble();
    }
    if (!to[0] || !to[1] || !to[2] || !contactProbeAt) return;
    const std::string at = m_selected;
    const JPSetupProperties::Row r = row;
    contactProbeAt(to, [this, r, at, alive = std::weak_ptr<bool>(m_alive)](double z) {
        if (const auto a = alive.lock(); !a || !*a || m_selected != at || r.cells.size() < 3) return;
        if (!m_form->set(r.cells[2].property, JVariant(z))) return;
        rebuildTree();
        record("Contact probe " + nameOf(m_draft, at) + ": " + r.label, "", at);
    });
}

void JPMachineSetupPanel::update() {
    // OpenPnP's actions for each kind of part: Delete's name, icon and words, and whether it moves up and down.
    struct Kind { const char* kind; const char* name; const char* icon; bool permutes; };
    static const Kind kKinds[] = {
        { "driver", "driver", "general-remove", true },     { "axis", "axis", "general-remove", true },
        { "actuator", "actuator", "general-remove", true }, { "camera", "camera", "general-remove", false },
        { "nozzle", "nozzle", "nozzle-remove", false },     { "nozzletip", "nozzle tip", "nozzletip-remove", false },
        { "signaler", "signaler", "general-remove", false }, { "head", "head", "general-remove", false },
        { "step", "step", "general-remove", true },
    };
    const std::string what = JPSetupEdits::addable(m_draft, m_selected);
    const std::string kind = JPSetupTree::parse(m_selected).kind;
    const Kind* k = nullptr;
    for (const Kind& c : kKinds)
        if (kind == c.kind) k = &c;
    // A step of unloading that is loading backwards is shown, not changed.
    const bool part = k && (kind != "step" || !what.empty());
    auto title = [](std::string s) {
        for (size_t i = 0; i < s.size(); ++i)
            if (i == 0 || s[i - 1] == ' ') s[i] = char(std::toupper(static_cast<unsigned char>(s[i])));
        return s;
    };
    m_tools->clear();   // the buttons are kept here
    if (kind == "nozzletip") m_tools->add(m_unload.get())->add(m_load.get());
    if (part) m_tools->add(m_remove.get());
    if (part && k->permutes) m_tools->add(m_up.get())->add(m_down.get());
    if (!what.empty()) m_tools->add(m_add.get());
    if (part) {
        m_remove->setIcon(k->icon);
        m_remove->setTooltip("Delete the currently selected " + std::string(k->name) + ".");
        m_up->setTooltip("Move the currently selected " + std::string(k->name) + " one position up.");
        m_down->setTooltip("Move the currently selected " + std::string(k->name) + " one position down.");
    }
    m_menuRemove->setLabel(part ? "Delete " + title(k->name) + "..." : std::string("Delete..."));
    m_menuRemove->setEnabled(part);
    // The group's New: OpenPnP's own icon for nozzles and nozzle tips; "…" where a kind is chosen first.
    m_add->setIcon(what == "Nozzle" ? "nozzle-add" : what == "Nozzle Tip" ? "nozzletip-add" : "general-add");
    std::string lower = what == "Controller" ? "driver" : what;
    for (char& c : lower) c = char(std::tolower(static_cast<unsigned char>(c)));
    m_add->setTooltip("Create a new " + lower + ".");
    const bool chooses = !JPSetupEdits::kinds(m_draft, m_selected).empty();
    m_add->setLeads(chooses ? JPIconButton::Leads::Elsewhere : JPIconButton::Leads::Nowhere);
    m_menuAdd->setLabel(what.empty() ? "New" : "New " + std::string(what == "Controller" ? "Driver" : what) + (chooses ? "..." : ""));
    m_menuAdd->setEnabled(!what.empty());

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
