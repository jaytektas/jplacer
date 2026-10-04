// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPPanelDefinitionPanel.h"

#include "JPUiParts.h"

#include "model/JPBoardLocation.h"
#include "model/JPDefinitionChanges.h"
#include "model/JPSides.h"

#include <j/core/Dialog.h>
#include <j/core/JSeparator.h>

#include <algorithm>
#include <cctype>
#include <set>

inline namespace jf {

namespace {

std::unique_ptr<JSeparator> toolSeparator(JSceneGraph& graph) {
    return std::make_unique<JSeparator>(graph, JSeparator::JOrientation::Vertical, JPIconButton::size());
}

std::string withSuffix(std::string path, const std::string& suffix) {
    std::string lower = path;
    for (char& c : lower) c = char(std::tolower(uint8_t(c)));
    if (lower.size() < suffix.size() || lower.compare(lower.size() - suffix.size(), suffix.size(), suffix) != 0)
        path += suffix;
    return path;
}

std::string trimmed(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

// Whether `child` (a panel) is the panel `root`, or has it among its panels.
bool circular(const JPPanel& root, const JPPanel& child) {
    if (child.file == root.file) return true;
    for (const auto& c : child.children)
        if (c->kind() == JPPlacementsHolderLocation::Kind::Panel && c->holder &&
            circular(root, *static_cast<const JPPanel*>(c->holder.get())))
            return true;
    return false;
}

} // namespace

JPPanelDefinitionPanel::JPPanelDefinitionPanel(JSceneGraph& graph, JPConfiguration& config,
                                               std::function<const JPJob*()> job)
    : JPGroupFrame(graph, "Panel Definition")
    , m_config(config)
    , m_job(job)
    , m_children(config, JPLocationsTableModel::Mode::PanelDefinition,
                 { JPLocationsTableModel::kId, JPLocationsTableModel::kName, JPLocationsTableModel::kWidth,
                   JPLocationsTableModel::kLength, JPLocationsTableModel::kSide, JPLocationsTableModel::kX,
                   JPLocationsTableModel::kY, JPLocationsTableModel::kRotation, JPLocationsTableModel::kEnabled,
                   JPLocationsTableModel::kCheckFids },
                 job)
    , m_fiducials(config, job,
                  { JPPlacementsTableModel::kEnabled, JPPlacementsTableModel::kId, JPPlacementsTableModel::kPart,
                    JPPlacementsTableModel::kSide, JPPlacementsTableModel::kX, JPPlacementsTableModel::kY,
                    JPPlacementsTableModel::kRotation, JPPlacementsTableModel::kComments }) {
    setAlignItems(JAlignItems::Stretch);
    setVSizePolicy(JSizePolicyMode::Expanding, 1);
    m_children.onChanged = [this] { changed(); };
    m_fiducials.onChanged = [this] { changed(); };

    // Children.
    m_childrenPane = std::make_unique<JContainer>(graph, 0.f, 0.f);
    m_childrenPane->setDirection(JFlexDirection::Column)->setAlignItems(JAlignItems::Stretch);
    auto children = std::make_unique<JPGroupFrame>(graph, "Children");
    children->setAlignItems(JAlignItems::Stretch);
    children->setVSizePolicy(JSizePolicyMode::Expanding, 1);
    auto bar = JPUiParts::row(graph);
    auto tool = [&](JContainer& on, const char* name, const char* icon, const char* tip) {
        return on.add(std::make_unique<JPIconButton>(graph, name, icon, tip));
    };
    m_add = tool(*bar, "Add Child...", "general-add", "Add a new or existing board/panel to this panel");
    m_add->setLeads(JPIconButton::Leads::Menu);
    m_add->onClicked.connect([this] { showAddMenu(); });
    m_remove = tool(*bar, "Remove Child(ren)", "general-remove",
                    "Remove the selected board(s) and/or panel(s) from this panel");
    m_remove->onClicked.connect([this] { removeChildren(); });
    bar->add(toolSeparator(graph));
    m_array = tool(*bar, "Create array of children...", "panelize", "Create an array of children from the selected child");
    m_array->setLeads(JPIconButton::Leads::Elsewhere);
    m_array->onClicked.connect([this] {
        const auto s = childSelections();
        if (s.size() != 1 || !openArray || !panel()) return;
        openArray(m_root, *s.front(), [this] { changed(); refresh(); });
    });
    bar->add(toolSeparator(graph));
    m_view = tool(*bar, "View Panel", "color-true", "Display a graphical representation of the panel");
    m_view->setLeads(JPIconButton::Leads::Elsewhere);
    m_view->onClicked.connect([this] {
        if (panel() && onViewPanel) onViewPanel();
    });
    children->add(std::move(bar));
    m_childTable = children->add(std::make_unique<JPTable>(graph));
    m_childTable->setModel(&m_children);
    m_childTable->openMenu = [this](JMenu* m, float x, float y) { if (openMenu) openMenu(m, x, y); };
    m_childTable->onSelectionChanged.connect([this] { updateActions(); });
    m_childTable->onEditRefused = [](const std::string&) {};
    m_childTable->onKey = [this](const JKeyEvent& ke) {
        if (ke.key != JKeyEvent::JKey::Space || ke.ctrl || ke.alt) return false;
        const auto s = childSelections();
        if (s.empty()) return false;
        const bool on = !s.front()->locallyEnabled;
        m_children.edit(s.front(), [on](JPPlacementsHolderLocation& c) { c.locallyEnabled = on; });
        m_childTable->refresh();
        return true;
    };
    m_childrenPane->add(std::move(children));

    // Alignment fiducials and placements.
    m_fiducialsPane = std::make_unique<JContainer>(graph, 0.f, 0.f);
    m_fiducialsPane->setDirection(JFlexDirection::Column)->setAlignItems(JAlignItems::Stretch);
    auto fiducials = std::make_unique<JPGroupFrame>(graph, "Alignment Fiducials/Placements");
    fiducials->setAlignItems(JAlignItems::Stretch);
    fiducials->setVSizePolicy(JSizePolicyMode::Expanding, 1);
    auto fbar = JPUiParts::row(graph);
    m_addFiducial = tool(*fbar, "Add Fiducial...", "general-add", "Add a fiducial to this panel");
    m_addFiducial->setLeads(JPIconButton::Leads::Elsewhere);
    m_addFiducial->onClicked.connect([this] { addFiducial(); });
    m_removeFiducial = tool(*fbar, "Remove Fiducial(s)...", "general-remove", "Remove the currently selected fiducial(s)");
    m_removeFiducial->onClicked.connect([this] { removeFiducials(); });
    m_useChildren = tool(*fbar, "Use Children Fiducials...", "panelize_use_board_fiducial",
                         "Use children's fiducials/placements for panel alignment");
    m_useChildren->setLeads(JPIconButton::Leads::Elsewhere);
    m_useChildren->onClicked.connect([this] { useChildFiducials(); });
    fiducials->add(std::move(fbar));
    m_fiducialTable = fiducials->add(std::make_unique<JPTable>(graph));
    m_fiducialTable->setModel(&m_fiducials);
    m_fiducialTable->openMenu = [this](JMenu* m, float x, float y) { if (openMenu) openMenu(m, x, y); };
    m_fiducialTable->onSelectionChanged.connect([this] { updateActions(); });
    m_fiducialTable->onEditRefused = [](const std::string&) {};
    m_fiducialsPane->add(std::move(fiducials));

    m_split = add(std::make_unique<JSplitter>(graph, JSplitter::JOrientation::Vertical, 0.f, 0.f));
    m_split->setHostsPanes(true);
    m_split->setVSizePolicy(JSizePolicyMode::Expanding, 1);
    m_split->addPane(m_childrenPane.get(), 0.5f);
    m_split->addPane(m_fiducialsPane.get(), 0.5f);

    buildMenus();
    m_childTable->setContextMenu(m_childMenu.get());
    m_childTable->onContextMenu = [this](int) { updateActions(); };
    m_fiducialTable->setContextMenu(m_fiducialMenu.get());
    m_fiducialTable->onContextMenu = [this](int) { updateActions(); };
    setPanel(nullptr);
}

void JPPanelDefinitionPanel::buildMenus() {
    JSceneGraph& g = m_graph;
    auto sub = [&](JMenu& on, const std::string& title) {
        m_subMenus.push_back(std::make_unique<JMenu>(title));
        on.add(g, title, {}, m_subMenus.back().get());
        return m_subMenus.back().get();
    };
    // Children: each change to the chosen children.
    m_childMenu = std::make_unique<JMenu>("Children");
    m_replace = m_childMenu->add(g, "Replace child(ren)...");
    m_replace->onTriggered.connect([this] { replaceChildren(); });
    JMenu* side = sub(*m_childMenu, "Set Side...");
    for (JPSide s : { JPSide::Bottom, JPSide::Top })
        side->add(g, JPSides::name(s))->onTriggered.connect([this, s] {
            for (JPPlacementsHolderLocation* c : childSelections()) m_children.setSide(c, s);
            m_childTable->refresh();
        });
    JMenu* enabled = sub(*m_childMenu, "Set Enabled...");
    for (bool on : { true, false })
        enabled->add(g, on ? "Enabled" : "Disabled")->onTriggered.connect([this, on] {
            for (JPPlacementsHolderLocation* c : childSelections())
                m_children.edit(c, [on](JPPlacementsHolderLocation& x) { x.locallyEnabled = on; });
            m_childTable->refresh();
        });
    JMenu* fids = sub(*m_childMenu, "Set Check Fids...");
    for (bool check : { true, false })
        fids->add(g, check ? "Check" : "Don't Check")->onTriggered.connect([this, check] {
            for (JPPlacementsHolderLocation* c : childSelections())
                m_children.edit(c, [check](JPPlacementsHolderLocation& x) { x.checkFiducials = check; });
            m_childTable->refresh();
        });

    // Alignment fiducials: their side, turned on or off.
    m_fiducialMenu = std::make_unique<JMenu>("Fiducials");
    JMenu* fside = sub(*m_fiducialMenu, "Set Side...");
    for (JPSide s : { JPSide::Bottom, JPSide::Top })
        fside->add(g, JPSides::name(s))->onTriggered.connect([this, s] {
            for (const JPPlacement* p : fiducialSelections()) m_fiducials.edit(p->id, [s](JPPlacement& q) { q.side = s; });
            m_fiducials.reload();
            m_fiducialTable->refresh();
        });
    JMenu* fenabled = sub(*m_fiducialMenu, "Set Enabled...");
    for (bool on : { true, false })
        fenabled->add(g, on ? "Enabled" : "Disabled")->onTriggered.connect([this, on] {
            const int col = m_fiducials.columnOf(JPPlacementsTableModel::kEnabled);
            for (const int r : m_fiducialTable->selectedRows()) m_fiducials.setChecked(r, col, on);
            m_fiducials.reload();
            m_fiducialTable->refresh();
        });
}

void JPPanelDefinitionPanel::setPanel(std::shared_ptr<JPPanel> panel) {
    m_root.holder = std::move(panel);
    m_root.setGlobalSide(JPSide::Top);
    if (m_root.holder) m_root.setParentsOfAllDescendants();
    refresh();
}

void JPPanelDefinitionPanel::refresh() {
    if (m_root.holder) m_root.setParentsOfAllDescendants();
    m_children.setRows(&m_root, m_root.holder ? m_root.children() : std::vector<JPPlacementsHolderLocation*> {});
    m_fiducials.setHolder(panel());
    m_childTable->refresh();
    m_fiducialTable->refresh();
    updateActions();
}

std::vector<JPPlacementsHolderLocation*> JPPanelDefinitionPanel::childSelections() const {
    std::vector<JPPlacementsHolderLocation*> out;
    for (const int r : m_childTable->selectedRows())
        if (JPPlacementsHolderLocation* l = m_children.location(r)) out.push_back(l);
    return out;
}

std::vector<JPPlacement*> JPPanelDefinitionPanel::fiducialSelections() const {
    std::vector<JPPlacement*> out;
    for (const int r : m_fiducialTable->selectedRows())
        if (JPPlacement* p = m_fiducials.placement(r)) out.push_back(p);
    return out;
}

void JPPanelDefinitionPanel::updateActions() {
    const bool has = panel() != nullptr;
    m_add->setEnabled(has);
    m_view->setEnabled(has);
    m_addFiducial->setEnabled(has);
    m_useChildren->setEnabled(has);
    const auto children = childSelections();
    // As OpenPnP's action groups: most for one or more children, the array for one;
    // Replace for those of one board or panel.
    m_remove->setEnabled(!children.empty());
    m_array->setEnabled(children.size() == 1);
    bool sameDefinition = !children.empty();
    for (const JPPlacementsHolderLocation* c : children)
        sameDefinition = sameDefinition && c->holder->definition() == children.front()->holder->definition();
    for (const auto& item : m_childMenu->items()) item->setEnabled(!children.empty());
    m_replace->setEnabled(sameDefinition);
    const bool anyFiducial = !fiducialSelections().empty();
    m_removeFiducial->setEnabled(anyFiducial);
    for (const auto& item : m_fiducialMenu->items()) item->setEnabled(anyFiducial);
}

void JPPanelDefinitionPanel::changed() {
    if (onChanged) onChanged();
}

void JPPanelDefinitionPanel::showAddMenu() {
    if (!openMenu || !panel()) return;
    JSceneGraph& g = m_graph;
    m_addMenu = std::make_unique<JMenu>("Add Child");
    m_addMenu->add(g, "Add New Board...")->onTriggered.connect([this] {
        JDialog::saveFile("Save New Board As...", { "xml" }, [this](std::string path) {
            addBoard(withSuffix(path, ".board.xml"), "Unable to create new board");
        });
    });
    m_addMenu->add(g, "Add Existing Board...")->onTriggered.connect([this] {
        if (chooseExisting)
            chooseExisting("Select existing board...", "board",
                           [this](std::string path) { addBoard(path, "Board load failed"); });
    });
    m_addMenu->addSeparator(g);
    m_addMenu->add(g, "Add New Panel...")->onTriggered.connect([this] {
        JDialog::saveFile("Save New Panel As...", { "xml" }, [this](std::string path) {
            addPanel(withSuffix(path, ".panel.xml"), "Unable to create new panel");
        });
    });
    m_addMenu->add(g, "Add Existing Panel...")->onTriggered.connect([this] {
        if (chooseExisting)
            chooseExisting("Select existing panel...", "panel",
                           [this](std::string path) { addPanel(path, "Panel load failed"); });
    });
    const JRect b = m_graph.getLayoutConst(m_add->getNodeId()).boundingBox;
    openMenu(m_addMenu.get(), b.x + b.width, b.y + b.height);
}

void JPPanelDefinitionPanel::addBoard(const std::string& path, const char* errorTitle) {
    if (!panel()) return;
    std::string error;
    const auto board = m_config.board(path, error);
    if (!board) {
        JDialog::message(errorTitle, error);
        return;
    }
    auto l = std::make_unique<JPBoardLocation>();
    l->holder = board->instance();
    l->parent = &m_root;
    JPPlacementsHolderLocation* added = JPDefinitionChanges(m_config, m_job()).childAdded(*panel(), std::move(l));
    refresh();
    m_childTable->selectRow(m_children.rowOf(added));
    changed();
}

void JPPanelDefinitionPanel::addPanel(const std::string& path, const char* errorTitle) {
    if (!panel()) return;
    std::string error;
    const auto p = m_config.panel(path, error);
    if (!p) {
        JDialog::message(errorTitle, error);
        return;
    }
    if (circular(*panel(), *p)) {
        JDialog::message(errorTitle, "A panel can not be made a descendant of itself");
        return;
    }
    auto l = std::make_unique<JPPanelLocation>();
    l->holder = p->instance();
    l->parent = &m_root;
    JPPlacementsHolderLocation* added = JPDefinitionChanges(m_config, m_job()).childAdded(*panel(), std::move(l));
    refresh();
    m_childTable->selectRow(m_children.rowOf(added));
    changed();
}

void JPPanelDefinitionPanel::removeChildren() {
    if (!panel()) return;
    std::vector<std::string> ids;
    for (const JPPlacementsHolderLocation* c : childSelections()) ids.push_back(c->id);
    JPDefinitionChanges changes(m_config, m_job());
    for (const std::string& id : ids) changes.childRemoved(*panel(), id);
    refresh();
    changed();
}

void JPPanelDefinitionPanel::replaceChildren() {
    const auto chosen = childSelections();
    if (chosen.empty() || !chooseExisting || !panel()) return;
    const bool board = chosen.front()->kind() == JPPlacementsHolderLocation::Kind::Board;
    std::vector<std::string> ids;
    for (const JPPlacementsHolderLocation* c : chosen) ids.push_back(c->id);
    chooseExisting(board ? "Select existing board..." : "Select existing panel...", board ? "board" : "panel",
                   [this, ids, board](std::string path) {
                       if (!panel()) return;
                       std::string error;
                       std::unique_ptr<JPPlacementsHolderLocation> replacement;
                       if (board) {
                           const auto b = m_config.board(path, error);
                           if (b) {
                               replacement = std::make_unique<JPBoardLocation>();
                               replacement->holder = b->instance();
                           }
                       } else {
                           const auto p = m_config.panel(path, error);
                           if (p) {
                               replacement = std::make_unique<JPPanelLocation>();
                               replacement->holder = p->instance();
                           }
                       }
                       if (!replacement) {
                           JDialog::message("Board or Panel load failed", error);
                           return;
                       }
                       JPDefinitionChanges changes(m_config, m_job());
                       for (const std::string& id : ids) changes.childReplaced(*panel(), id, *replacement);
                       refresh();
                       changed();
                   });
}

void JPPanelDefinitionPanel::addFiducial() {
    if (!panel()) return;
    if (m_config.parts().empty()) {
        JDialog::message("Error", "There are currently no parts defined in the system. Please create at least one part "
                                  "before creating a placement");
        return;
    }
    JDialog::input("Input", "Please enter an ID for the new fiducial.", [this](std::string text) {
        const std::string id = trimmed(text);
        if (id.empty() || !panel()) return;
        if (panel()->find(id)) {
            JDialog::message("Error", "The ID for the new fiducial already exists");
            return;
        }
        JPPlacement p;
        p.id = id;
        p.partId = m_config.parts().front()->id;
        p.location = JPLocation(JPLengthUnit::Millimeters);
        p.side = m_root.globalSide();
        p.type = JPPlacement::Type::Fiducial;
        JPDefinitionChanges(m_config, m_job()).added(*panel(), p);
        refresh();
        m_fiducialTable->selectRow(m_fiducials.rowOf(id));
        changed();
    });
}

void JPPanelDefinitionPanel::removeFiducials() {
    JPPanel* def = panel();
    if (!def) return;
    std::vector<std::string> own, pseudo;
    for (const int r : m_fiducialTable->selectedRows())
        if (const JPPlacement* p = m_fiducials.placement(r)) (m_fiducials.isPseudo(r) ? pseudo : own).push_back(p->id);
    JPDefinitionChanges changes(m_config, m_job());
    for (const std::string& id : own) changes.removed(*def, id);
    if (!pseudo.empty()) {
        std::erase_if(def->pseudoPlacementIds, [&pseudo](const std::string& id) {
            return std::find(pseudo.begin(), pseudo.end(), id) != pseudo.end();
        });
        changes.pseudoPlacementsChanged(*def);
    }
    refresh();
    changed();
}

void JPPanelDefinitionPanel::useChildFiducials() {
    if (!panel() || !chooseChildFiducials) return;
    chooseChildFiducials(m_root, [this](std::vector<std::string> ids) {
        JPPanel* def = panel();
        if (!def) return;
        for (const std::string& id : ids)
            if (std::find(def->pseudoPlacementIds.begin(), def->pseudoPlacementIds.end(), id) == def->pseudoPlacementIds.end())
                def->pseudoPlacementIds.push_back(id);
        JPDefinitionChanges(m_config, m_job()).pseudoPlacementsChanged(*def);
        refresh();
        changed();
    });
}

} // inline namespace jf
