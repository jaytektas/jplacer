// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPJobPlacementsPanel.h"

#include "JPUiParts.h"

#include "model/JPDefinitionChanges.h"
#include "model/JPSides.h"

#include <j/core/Dialog.h>
#include <j/core/JLabel.h>
#include <j/core/JSeparator.h>
#include <j/core/JStyle.h>
#include <j/core/JTextHelper.h>

inline namespace jf {

namespace {

// OpenPnP's search box is fifteen characters wide.
constexpr int kSearchColumns = 15;

std::unique_ptr<JSeparator> toolSeparator(JSceneGraph& graph) {
    return std::make_unique<JSeparator>(graph, JSeparator::JOrientation::Vertical, JPIconButton::size());
}

std::string trimmed(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

} // namespace

JPJobPlacementsPanel::JPJobPlacementsPanel(JSceneGraph& graph, JPConfiguration& config, std::function<JPJob*()> job)
    : JPGroupFrame(graph, "Placements")
    , m_config(config)
    , m_job(job)
    , m_model(config, [job] { return static_cast<const JPJob*>(job()); },
              { JPPlacementsTableModel::kEnabled, JPPlacementsTableModel::kId, JPPlacementsTableModel::kPart,
                JPPlacementsTableModel::kSide, JPPlacementsTableModel::kX, JPPlacementsTableModel::kY,
                JPPlacementsTableModel::kRotation, JPPlacementsTableModel::kType, JPPlacementsTableModel::kPlaced,
                JPPlacementsTableModel::kStatus, JPPlacementsTableModel::kErrorHandling, JPPlacementsTableModel::kRank,
                JPPlacementsTableModel::kComments }) {
    setAlignItems(JAlignItems::Stretch);
    setVSizePolicy(JSizePolicyMode::Expanding, 1);
    const JStyle& st = JStyle::current();
    m_model.onChanged = [this] {
        updateActivePlacements();
        changed();
    };
    m_model.hasFeeder = [&config](const std::string& partId) { return config.hasFeeder(partId); };

    auto bar = JPUiParts::row(graph);
    auto tool = [&](const char* name, const char* icon, const char* tip) {
        return bar->add(std::make_unique<JPIconButton>(graph, name, icon, tip));
    };
    m_new = tool("New Placement", "general-add", "Create a new placement and add it to the board.");
    m_new->setLeads(JPIconButton::Leads::Elsewhere);
    m_new->onClicked.connect([this] { newPlacement(); });
    m_remove = tool("Remove Placement(s)", "general-remove", "Remove the currently selected placement(s).");
    m_remove->onClicked.connect([this] { removePlacements(); });
    bar->add(toolSeparator(graph));
    m_cameraTo = tool("Move Camera To Placement Location", "position-camera", "Position the camera at the placement's location.");
    m_cameraTo->onClicked.connect([this] { moveTo(Tool::Camera, false); });
    m_cameraNext = tool("Move Camera To Next Placement Location", "position-camera-move-next",
                        "Position the camera at the next placements location.");
    m_cameraNext->onClicked.connect([this] { moveTo(Tool::Camera, true); });
    m_toolTo = tool("Move Tool To Placement Location", "position-nozzle", "Position the tool at the placement's location.");
    m_toolTo->onClicked.connect([this] { moveTo(Tool::Nozzle, false); });
    bar->add(toolSeparator(graph));
    m_captureCamera = tool("Capture Camera Placement Location", "capture-camera",
                           "Set the placement's location to the camera's current position.");
    m_captureCamera->onClicked.connect([this] { capture(Tool::Camera); });
    m_captureTool = tool("Capture Tool Placement Location", "capture-nozzle",
                         "Set the placement's location to the tool's current position.");
    m_captureTool->onClicked.connect([this] { capture(Tool::Nozzle); });
    bar->add(toolSeparator(graph));
    m_editFeeder = tool("Edit Placement Feeder", "feeder-edit", "Edit the placement's associated feeder definition.");
    m_editFeeder->setLeads(JPIconButton::Leads::Elsewhere);
    m_editFeeder->onClicked.connect([this] {
        if (const auto chosen = selections(); chosen.size() == 1 && onEditFeeder) onEditFeeder(chosen.front()->partId);
    });
    bar->add(std::make_unique<JContainer>(graph, 0.f, 0.f))->setHSizePolicy(JSizePolicyMode::Expanding, 1);
    JLabel* searchLabel = bar->add(std::make_unique<JLabel>(graph, "Search"));
    searchLabel->setFixedSize(JTextHelper::measureWidth("Search") + st.spacing, st.controlHeight);
    m_search = bar->add(std::make_unique<JLineEdit>(graph, ""));
    m_search->setFixedSize(JTextHelper::measureWidth("M") * kSearchColumns, st.controlHeight);
    m_search->onTextChanged.connect([this](const std::string& t) { m_table->setFilter(trimmed(t)); });
    add(std::move(bar));

    m_table = add(std::make_unique<JPTable>(graph));
    m_table->setModel(&m_model);
    m_table->openMenu = [this](JMenu* m, float x, float y) { if (openMenu) openMenu(m, x, y); };
    m_table->onSelectionChanged.connect([this] {
        updateActions();
        const auto s = selections();
        if (onPlacementChosen) onPlacementChosen(s.size() == 1 ? s.front() : nullptr);
    });
    m_table->onEditRefused = [](const std::string&) {};
    m_table->onKey = [this](const JKeyEvent& ke) {
        if (ke.key != JKeyEvent::JKey::Space || ke.ctrl || ke.alt) return false;
        const auto chosen = selections();
        if (chosen.empty()) return false;
        const bool on = !chosen.front()->enabled;
        m_model.edit(chosen.front()->id, [on](JPPlacement& p) { p.enabled = on; });
        m_table->refresh();
        return true;
    };
    buildMenu();
    m_table->setContextMenu(m_menu.get());
    m_table->onContextMenu = [this](int) { updateActions(); };
    setLocation(nullptr);
}

void JPJobPlacementsPanel::buildMenu() {
    JSceneGraph& g = m_graph;
    m_menu = std::make_unique<JMenu>("Placements");
    auto sub = [&](const std::string& title) {
        m_subMenus.push_back(std::make_unique<JMenu>(title));
        JMenuItem* item = m_menu->add(g, title, {}, m_subMenus.back().get());
        return std::pair { m_subMenus.back().get(), item };
    };
    auto forChosen = [this](std::function<void(JPPlacement&)> set) {
        return [this, set] {
            for (const JPPlacement* p : selections()) m_model.edit(p->id, set);
            m_table->refresh();
        };
    };
    auto [type, typeItem] = sub("Set Type");
    m_setType = typeItem;
    typeItem->setTooltip("Set the selected placement(s) type");
    for (JPPlacement::Type t : { JPPlacement::Type::Placement, JPPlacement::Type::Fiducial })
        type->add(g, JPPlacement::typeName(t))->onTriggered.connect(forChosen([t](JPPlacement& p) { p.type = t; }));
    auto [side, sideItem] = sub("Set Side");
    m_setSide = sideItem;
    sideItem->setTooltip("Set the selected placement(s) side");
    for (JPSide s : { JPSide::Bottom, JPSide::Top })
        side->add(g, JPSides::name(s))->onTriggered.connect(forChosen([s](JPPlacement& p) { p.side = s; }));
    auto [placed, placedItem] = sub("Set Placed");
    placedItem->setTooltip("Set the selected placement(s) status");
    for (bool on : { true, false })
        placed->add(g, on ? "Placed" : "Not Placed")->onTriggered.connect([this, on] {
            for (const int r : m_table->selectedRows()) m_model.setPlaced(r, on);
            m_table->refresh();
        });
    auto [enabled, enabledItem] = sub("Set Enabled");
    enabledItem->setTooltip("Set selected placement(s) enabled");
    for (bool on : { true, false })
        enabled->add(g, on ? "Enabled" : "Disabled")->onTriggered.connect(forChosen([on](JPPlacement& p) { p.enabled = on; }));
    auto [errors, errorsItem] = sub("Set Error Handling");
    errorsItem->setTooltip("Set the selected placement(s) error handling");
    for (JPPlacement::ErrorHandling e :
         { JPPlacement::ErrorHandling::Default, JPPlacement::ErrorHandling::Alert, JPPlacement::ErrorHandling::Defer })
        errors->add(g, JPPlacement::errorHandlingName(e))
            ->onTriggered.connect(forChosen([e](JPPlacement& p) { p.errorHandling = e; }));
}

void JPJobPlacementsPanel::setLocation(JPPlacementsHolderLocation* location) {
    m_location = location;
    JPJob* job = m_job();
    m_topLevel = location && job && location->parent == &job->root();
    m_singleInstance = location && job && location->holder && job->instanceCount(*location->holder) == 1;
    const bool editDefinition =
        m_topLevel && m_singleInstance && location->kind() == JPPlacementsHolderLocation::Kind::Board;
    m_model.setLocation(location, editDefinition, job);
    m_table->refresh();
    updateActions();
    updateActivePlacements();
}

void JPJobPlacementsPanel::select(const std::string& placementId) {
    m_search->setText("");
    m_table->setFilter("");
    m_table->selectRow(m_model.rowOf(placementId));
}

void JPJobPlacementsPanel::selectPlacement(const std::string& placementId) {
    m_table->selectRow(m_model.rowOf(placementId));
}

void JPJobPlacementsPanel::refresh() {
    m_model.reload();
    m_table->refresh();
    updateActions();
    updateActivePlacements();
}

void JPJobPlacementsPanel::updateActivePlacements() {
    JPJob* job = m_job();
    if (!job || !onCompletion) return;
    const int active = job->activePlacements(&job->root()), total = job->totalActivePlacements(&job->root());
    const int boardActive = m_location ? job->activePlacements(m_location) : 0;
    const int boardTotal = m_location ? job->totalActivePlacements(m_location) : 0;
    onCompletion(total - active, total, boardTotal - boardActive, boardTotal);
}

std::vector<JPPlacement*> JPJobPlacementsPanel::selections() const {
    std::vector<JPPlacement*> out;
    if (!m_location) return out;
    for (const int r : m_table->selectedRows())
        if (JPPlacement* p = m_model.placement(r)) out.push_back(p);
    return out;
}

void JPJobPlacementsPanel::updateActions() {
    const auto chosen = selections();
    const bool editDefinition =
        m_topLevel && m_singleInstance && m_location && m_location->kind() == JPPlacementsHolderLocation::Kind::Board;
    // As OpenPnP's action groups.
    m_new->setEnabled(editDefinition);
    m_remove->setEnabled(editDefinition);
    m_setType->setEnabled(editDefinition);
    m_setSide->setEnabled(editDefinition);
    const bool one = chosen.size() == 1;
    const bool facingUp = one && m_location && chosen.front()->side == m_location->globalSide();
    m_cameraTo->setEnabled(facingUp);
    m_cameraNext->setEnabled(facingUp);
    m_toolTo->setEnabled(facingUp);
    m_captureCamera->setEnabled(editDefinition && facingUp);
    m_captureTool->setEnabled(editDefinition && facingUp);
    m_editFeeder->setEnabled(one && chosen.front()->type == JPPlacement::Type::Placement && m_location
                             && m_location->kind() == JPPlacementsHolderLocation::Kind::Board);
    for (size_t i = 2; i < m_menu->items().size(); ++i) m_menu->items()[i]->setEnabled(!chosen.empty());
}

void JPJobPlacementsPanel::changed() {
    if (onChanged) onChanged();
}

void JPJobPlacementsPanel::newPlacement() {
    if (!m_location || !m_location->holder) return;
    if (m_config.parts().empty()) {
        JDialog::message("Error", "There are currently no parts defined in the system. Please create at least one "
                                  "part before creating a placement.");
        return;
    }
    JPPlacementsHolderLocation* at = m_location;
    JDialog::input("Input", "Please enter an ID for the new placement.", [this, at](std::string text) {
        const std::string id = trimmed(text);
        if (id.empty() || at != m_location) return;
        if (at->holder->find(id)) {
            JDialog::message("Error", "The ID for the new placement already exists");
            return;
        }
        JPPlacementsHolder* def = m_config.definitionOf(*at->holder);
        if (!def) return;
        JPPlacement p;
        p.id = id;
        p.partId = m_config.parts().front()->id;
        p.location = JPLocation(JPLengthUnit::Millimeters);
        p.side = at->globalSide();
        if (at->kind() == JPPlacementsHolderLocation::Kind::Panel) p.type = JPPlacement::Type::Fiducial;
        JPDefinitionChanges(m_config, m_job()).added(*def, p);
        if (JPJob* job = m_job()) {
            job->removePlacedStatus(*at, id);
            job->dirty = true;
        }
        refresh();
        m_table->selectRow(m_model.rowOf(id));
        changed();
    });
}

void JPJobPlacementsPanel::removePlacements() {
    if (!m_location || !m_location->holder) return;
    JPPlacementsHolder* def = m_config.definitionOf(*m_location->holder);
    if (!def) return;
    std::vector<std::string> ids;
    for (const JPPlacement* p : selections()) ids.push_back(p->id);
    JPDefinitionChanges changes(m_config, m_job());
    for (const std::string& id : ids) changes.removed(*def, id);
    if (JPJob* job = m_job()) job->dirty = true;
    refresh();
    changed();
}

void JPJobPlacementsPanel::moveTo(Tool tool, bool next) {
    if (!m_location || !moveTool) return;
    if (next) {
        // The next row shown chosen first.
        const auto rows = m_table->selectedRows();
        const int view = rows.empty() ? -1 : m_table->viewIndexOf(rows.front());
        const int to = m_table->modelRowAt(view + 1);
        if (to >= 0) m_table->selectRow(to);
    }
    const auto chosen = selections();
    if (chosen.empty()) return;
    moveTool(tool, m_location->placementLocation(chosen.front()->location));
}

void JPJobPlacementsPanel::capture(Tool tool) {
    const auto chosen = selections();
    if (chosen.empty() || !m_location || !toolLocation) return;
    const auto at = toolLocation(tool);
    if (!at) {
        JDialog::message("Error", "The machine is not connected.");
        return;
    }
    // Where the tool is, on the board, at the board's surface.
    const JPLocation local = m_location->placementLocationInverse(*at).derive(std::nullopt, std::nullopt, 0.0, std::nullopt);
    m_model.edit(chosen.front()->id, [&local](JPPlacement& p) { p.location = local; });
    m_table->refresh();
}

} // inline namespace jf
