// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPJobPanel.h"

#include <j/core/FrameTimer.h>

#include "JPUiParts.h"

#include "model/JPBoardLocation.h"
#include "model/JPPanelLocation.h"
#include "model/JPSides.h"

#include <j/core/Dialog.h>
#include <j/core/JSeparator.h>

#include <algorithm>
#include <cctype>

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

} // namespace

JPJobPanel::JPJobPanel(JSceneGraph& graph, JPConfiguration& config, std::function<JPJob*()> job, double split)
    : JContainer(graph, 0.f, 0.f)
    , m_config(config)
    , m_job(job)
    , m_model(config, JPLocationsTableModel::Mode::Job,
              { JPLocationsTableModel::kId, JPLocationsTableModel::kName, JPLocationsTableModel::kWidth,
                JPLocationsTableModel::kLength, JPLocationsTableModel::kSide, JPLocationsTableModel::kX,
                JPLocationsTableModel::kY, JPLocationsTableModel::kZ, JPLocationsTableModel::kRotation,
                JPLocationsTableModel::kEnabled, JPLocationsTableModel::kCheckFids },
              [job] { return static_cast<const JPJob*>(job()); }) {
    setDirection(JFlexDirection::Column)->setAlignItems(JAlignItems::Stretch);
    m_model.onChanged = [this] {
        if (JPJob* j = m_job()) j->dirty = true;
        m_placements->refresh();
        changed();
    };

    // Boards: the toolbar over the table.
    m_boardsPane = std::make_unique<JContainer>(graph, 0.f, 0.f);
    m_boardsPane->setDirection(JFlexDirection::Column)->setAlignItems(JAlignItems::Stretch);
    auto boards = std::make_unique<JPGroupFrame>(graph, "Boards");
    boards->setAlignItems(JAlignItems::Stretch);
    boards->setVSizePolicy(JSizePolicyMode::Expanding, 1);
    auto bar = JPUiParts::row(graph);
    auto tool = [&](const char* name, const char* icon, const char* tip) {
        return bar->add(std::make_unique<JPIconButton>(graph, name, icon, tip));
    };
    m_start = tool("Start", "control-start", "Start processing the job.");
    m_start->onClicked.connect([this] {
        if (onStartPauseResume) onStartPauseResume();
    });
    m_step = tool("Step", "control-next", "Process one step of the job and pause.");
    m_step->onClicked.connect([this] {
        if (onStep) onStep();
    });
    m_stop = tool("Stop", "control-stop", "Stop processing the job.");
    m_stop->onClicked.connect([this] {
        if (onStop) onStop();
    });
    bar->add(toolSeparator(graph));
    m_errors = tool("Alert Errors", "error-alert", "");
    m_errors->onClicked.connect([this] {
        JPJob* j = m_job();
        if (!j) return;
        j->errorHandling = j->errorHandling == JPJob::ErrorHandling::Defer ? JPJob::ErrorHandling::Alert
                                                                          : JPJob::ErrorHandling::Defer;
        j->dirty = true;
        updateJobActions();
        changed();
    });
    bar->add(toolSeparator(graph));
    m_add = tool("Add Board/Panel", "general-add", "Add a new or existing board or panel to the job.");
    m_add->setLeads(JPIconButton::Leads::Menu);
    m_add->onClicked.connect([this] { showAddMenu(); });
    m_remove = tool("Remove Board(s)/Panel(s)", "general-remove", "Remove the selected board(s) and/or panel(s) from the job.");
    m_remove->onClicked.connect([this] { removeSelected(); });
    bar->add(toolSeparator(graph));
    m_cameraTo = tool("Move Camera To Board Location", "position-camera", "Position the camera at the board's location.");
    m_cameraTo->onClicked.connect([this] { moveTo(Tool::Camera, false); });
    m_cameraNext = tool("Move Camera to the Next Board", "position-camera-move-next",
                        "Position the camera at the next board's location.");
    m_cameraNext->onClicked.connect([this] { moveTo(Tool::Camera, true); });
    m_toolTo = tool("Move Tool To Board Location", "position-nozzle", "Position the tool at the board's location.");
    m_toolTo->onClicked.connect([this] { moveTo(Tool::Nozzle, false); });
    bar->add(toolSeparator(graph));
    m_captureCamera = tool("Capture Camera Location", "capture-camera",
                           "Set the board's X, Y, and Rotation to the camera's current X, Y, and Rotation.");
    m_captureCamera->onClicked.connect([this] { captureCamera(); });
    m_captureTool = tool("Capture Tool Location", "capture-nozzle", "Set the board's Z to the tool's current Z.");
    m_captureTool->onClicked.connect([this] { captureTool(); });
    bar->add(toolSeparator(graph));
    m_twoPoint = tool("Multiple Point Board Location", "board-two-placement-locate",
                      "Set the board's location and rotation using multiple placements.");
    m_twoPoint->setLeads(JPIconButton::Leads::Elsewhere);
    m_twoPoint->onClicked.connect([this] {
        const auto s = selections();
        if (s.size() != 1 || m_locating) return;
        JPBoardLocationProcess::Hooks h;
        auto tool = [](JPBoardLocationProcess::Tool t) {
            return t == JPBoardLocationProcess::Tool::Camera ? Tool::Camera : Tool::Nozzle;
        };
        h.toolLocation = [this, tool](JPBoardLocationProcess::Tool t) {
            return toolLocation ? toolLocation(tool(t)) : std::nullopt;
        };
        h.moveTool = [this, tool](JPBoardLocationProcess::Tool t, const JPLocation& at) {
            if (moveTool) moveTool(tool(t), at);
        };
        h.chosenPlacements = [this] { return m_placements->selections(); };
        h.selectPlacement = [this](const std::string& id) { m_placements->select(id); };
        h.show = [this](const std::string& title, const std::string& text, const std::string& proceed,
                        std::function<void()> cancel, std::function<void()> next) {
            showInstructions(title, text, proceed, std::move(cancel), std::move(next));
        };
        h.finished = [this] {
            hideInstructions();
            m_table->refresh();
            changed();
            // Not here: the process is the caller.
            jPostToNextFrame([this] { m_locating.reset(); });
        };
        m_locating = std::make_unique<JPBoardLocationProcess>(*s.front(), m_job() && s.front()->parent == &m_job()->root(), h);
    });
    m_fiducialCheck = tool("Fiducial Check", "board-fiducial-locate",
                           "Perform a fiducial check for the board and update it's location and rotation.");
    m_fiducialCheck->onClicked.connect([this] {
        const auto s = selections();
        if (s.size() == 1 && onFiducialCheck) onFiducialCheck(s.front());
    });
    bar->add(toolSeparator(graph));
    JPIconButton* view = tool("View Job", "color-true", "Display a graphical representation of the job");
    view->setLeads(JPIconButton::Leads::Elsewhere);
    view->onClicked.connect([this] {
        if (!onViewJob) return;
        std::vector<const JPPlacementsHolderLocation*> chosen;
        for (const JPPlacementsHolderLocation* l : selections()) chosen.push_back(l);
        onViewJob(chosen);
    });
    boards->add(std::move(bar));
    m_table = boards->add(std::make_unique<JPTable>(graph));
    m_table->setModel(&m_model);
    m_table->openMenu = [this](JMenu* m, float x, float y) { if (openMenu) openMenu(m, x, y); };
    m_table->onSelectionChanged.connect([this] { selectionChanged(); });
    m_table->onEditRefused = [](const std::string&) {};
    m_boardsPane->add(std::move(boards));

    m_placementsPane = std::make_unique<JContainer>(graph, 0.f, 0.f);
    m_placementsPane->setDirection(JFlexDirection::Column)->setAlignItems(JAlignItems::Stretch);
    m_placements = m_placementsPane->add(std::make_unique<JPJobPlacementsPanel>(graph, config, job));
    m_placements->openMenu = [this](JMenu* m, float x, float y) { if (openMenu) openMenu(m, x, y); };
    m_placements->toolLocation = [this](Tool t) { return toolLocation ? toolLocation(t) : std::nullopt; };
    m_placements->moveTool = [this](Tool t, const JPLocation& at) {
        if (moveTool) moveTool(t, at);
    };
    m_placements->onChanged = [this] {
        m_table->refresh();
        changed();
    };

    // The instructions of a process under way, across the top; nothing while none is.
    m_instructionsHolder = add(std::make_unique<JContainer>(graph, 0.f, 0.f));
    m_instructionsHolder->setDirection(JFlexDirection::Column)->setAlignItems(JAlignItems::Stretch);
    m_instructionsHolder->setVSizePolicy(JSizePolicyMode::Fixed);
    m_instructionsHolder->setFixedSize(0.f, 0.f);
    m_instructionsHolder->setHSizePolicy(JSizePolicyMode::Expanding, 1);
    m_instructions = std::make_unique<JPInstructions>(graph);
    m_split = add(std::make_unique<JSplitter>(graph, JSplitter::JOrientation::Vertical, 0.f, 0.f));
    m_split->setHostsPanes(true);
    m_split->setVSizePolicy(JSizePolicyMode::Expanding, 1);
    m_split->addPane(m_boardsPane.get(), float(split));
    m_split->addPane(m_placementsPane.get(), float(1 - split));

    buildMenu();
    m_table->setContextMenu(m_menu.get());
    m_table->onContextMenu = [this](int) { selectionChanged(); };
    refresh();
}

double JPJobPanel::split() const {
    const std::vector<float> f = m_split->fractions();
    return f.empty() ? 0.5 : f.front();
}

void JPJobPanel::buildMenu() {
    JSceneGraph& g = m_graph;
    m_menu = std::make_unique<JMenu>("Job");
    auto sub = [&](const std::string& title) {
        m_subMenus.push_back(std::make_unique<JMenu>(title));
        m_menu->add(g, title, {}, m_subMenus.back().get());
        return m_subMenus.back().get();
    };
    JMenu* side = sub("Set Side");
    for (JPSide s : { JPSide::Bottom, JPSide::Top })
        side->add(g, JPSides::name(s))->onTriggered.connect([this, s] {
            // Only those straight in the job are turned over.
            JPJob* j = m_job();
            if (!j) return;
            for (JPPlacementsHolderLocation* l : selections())
                if (l->parent == &j->root()) m_model.setSide(l, s);
            m_table->refresh();
            m_placements->refresh();
        });
    JMenu* enabled = sub("Set Enabled");
    for (bool on : { true, false })
        enabled->add(g, on ? "Enabled" : "Disabled")->onTriggered.connect([this, on] {
            for (JPPlacementsHolderLocation* l : selections())
                if (l->isParentBranchEnabled()) m_model.edit(l, [on](JPPlacementsHolderLocation& x) { x.locallyEnabled = on; });
            m_table->refresh();
        });
    JMenu* fids = sub("Set Check Fids");
    for (bool check : { true, false })
        fids->add(g, check ? "Check" : "Don't Check")->onTriggered.connect([this, check] {
            for (JPPlacementsHolderLocation* l : selections())
                m_model.edit(l, [check](JPPlacementsHolderLocation& x) { x.checkFiducials = check; });
            m_table->refresh();
        });
}

void JPJobPanel::refresh() {
    JPJob* j = m_job();
    // Every board and panel but the job's own root, nested in order.
    std::vector<JPPlacementsHolderLocation*> rows;
    if (j) {
        j->root().setParentsOfAllDescendants();
        for (JPPlacementsHolderLocation* l : j->boardAndPanelLocations())
            if (l != &j->root()) rows.push_back(l);
    }
    m_model.setRows(j ? &j->root() : nullptr, rows);
    m_table->refresh();
    selectionChanged();
    updateJobActions();
}

std::vector<JPPlacementsHolderLocation*> JPJobPanel::selections() const {
    std::vector<JPPlacementsHolderLocation*> out;
    for (const int r : m_table->selectedRows())
        if (JPPlacementsHolderLocation* l = m_model.location(r)) out.push_back(l);
    return out;
}

void JPJobPanel::selectionChanged() {
    const auto s = selections();
    JPJob* j = m_job();
    // As OpenPnP's action groups: what one board or panel allows, what several
    // do, and more when they lie straight in the job.
    bool single = false, multi = false, singleTop = false, multiTop = false;
    if (s.size() == 1) {
        (j && s.front()->parent == &j->root() ? singleTop : single) = true;
    } else if (s.size() > 1) {
        multiTop = true;
        for (const JPPlacementsHolderLocation* l : s) {
            bool ancestorChosen = false;
            for (const JPPlacementsHolderLocation* other : s)
                if (other != l && l->isDescendantOf(*other)) ancestorChosen = true;
            if (j && l->parent != &j->root() && !ancestorChosen) {
                multiTop = false;
                multi = true;
                break;
            }
        }
    }
    const bool any = single || multi || singleTop || multiTop;
    m_captureTool->setEnabled(any);
    m_remove->setEnabled(singleTop || multiTop);
    m_captureCamera->setEnabled(singleTop);
    for (JPIconButton* b : { m_cameraTo, m_cameraNext, m_toolTo, m_fiducialCheck, m_twoPoint })
        b->setEnabled(single || singleTop);
    for (const auto& item : m_menu->items()) item->setEnabled(any);
    m_placements->setLocation(s.size() == 1 ? s.front() : nullptr);
    if (onSelectionChanged) {
        std::vector<const JPPlacementsHolderLocation*> chosen(s.begin(), s.end());
        onSelectionChanged(chosen);
    }
}

void JPJobPanel::select(const std::string& uniqueId, const std::string& placementId) {
    for (int r = 0; r < m_model.rowCount(); ++r)
        if (const JPPlacementsHolderLocation* l = m_model.location(r); l && l->uniqueId() == uniqueId) {
            m_table->selectRow(r);
            if (!placementId.empty()) m_placements->select(placementId);
            return;
        }
}

void JPJobPanel::showInstructions(const std::string& title, const std::string& text, const std::string& proceedLabel,
                                  std::function<void()> onCancel, std::function<void()> onProceed) {
    m_instructions->set(title, text, proceedLabel, std::move(onCancel), std::move(onProceed));
    if (!m_instructionsShown) m_instructionsHolder->add(m_instructions.get());
    m_instructionsShown = true;
    m_instructionsHolder->setFixedSize(0.f, JPInstructions::height());
    invalidate();
}

void JPJobPanel::hideInstructions() {
    m_instructionsHolder->clear();
    m_instructionsShown = false;
    m_instructionsHolder->setFixedSize(0.f, 0.f);
    invalidate();
}

void JPJobPanel::setRunState(RunState s) {
    m_runState = s;
    updateJobActions();
}

void JPJobPanel::setMenuItems(JMenuItem* start, JMenuItem* step, JMenuItem* stop) {
    m_startItem = start;
    m_stepItem = step;
    m_stopItem = stop;
    updateJobActions();
}

void JPJobPanel::resetAllPlaced() {
    JPJob* j = m_job();
    if (!j) return;
    j->removeAllPlacedStatus();
    j->dirty = true;
    m_placements->refresh();
    changed();
}

void JPJobPanel::setMachineEnabled(bool on) {
    m_machineEnabled = on;
    updateJobActions();
}

void JPJobPanel::updateJobActions() {
    // As OpenPnP: Start becomes Pause while running and Resume while paused.
    const bool running = m_runState == RunState::Running;
    m_start->setIcon(running ? "control-pause" : "control-start");
    m_start->setTooltip(running                              ? "Pause processing of the job."
                        : m_runState == RunState::Paused     ? "Resume processing of the job."
                                                             : "Start processing the job.");
    const bool settled = m_runState == RunState::Stopped || m_runState == RunState::Paused || running;
    m_start->setEnabled(m_machineEnabled && settled);
    m_step->setEnabled(m_machineEnabled && (m_runState == RunState::Stopped || m_runState == RunState::Paused));
    m_stop->setEnabled(m_machineEnabled && (running || m_runState == RunState::Paused));
    if (m_startItem) {
        m_startItem->setLabel(running ? "Pause" : m_runState == RunState::Paused ? "Resume" : "Start");
        m_startItem->setEnabled(m_start->isEnabled());
    }
    if (m_stepItem) m_stepItem->setEnabled(m_step->isEnabled());
    if (m_stopItem) m_stopItem->setEnabled(m_stop->isEnabled());
    const JPJob* j = m_job();
    const bool defer = j && j->errorHandling == JPJob::ErrorHandling::Defer;
    m_errors->setIcon(defer ? "error-defer" : "error-alert");
    m_errors->setTooltip(defer ? "Errors will be reported at the end of a Job; for placements that have the \"Default\" "
                                 "error handling"
                               : "Job errors will be alerted immediately; for placements that have the \"Default\" "
                                 "error handling");
}

void JPJobPanel::changed() {
    if (onChanged) onChanged();
}

void JPJobPanel::showAddMenu() {
    if (!openMenu) return;
    JSceneGraph& g = m_graph;
    m_addMenu = std::make_unique<JMenu>("Add Board/Panel");
    m_addMenu->add(g, "New Board...")->onTriggered.connect([this] {
        JDialog::saveFile("Save New Board As...", { "xml" }, [this](std::string path) {
            addBoard(withSuffix(path, ".board.xml"), "Unable to create new board");
        });
    });
    m_addMenu->add(g, "Existing Board...")->onTriggered.connect([this] {
        if (chooseExisting)
            chooseExisting("Add existing board to job", "board", [this](std::string path) { addBoard(path, "Board load failed"); });
    });
    m_addMenu->addSeparator(g);
    m_addMenu->add(g, "New Panel...")->onTriggered.connect([this] {
        JDialog::saveFile("Save New Panel As...", { "xml" }, [this](std::string path) {
            addPanel(withSuffix(path, ".panel.xml"), "Unable to create new panel");
        });
    });
    m_addMenu->add(g, "Existing Panel...")->onTriggered.connect([this] {
        if (chooseExisting)
            chooseExisting("Add existing panel to job", "panel", [this](std::string path) { addPanel(path, "Panel load failed"); });
    });
    const JRect b = m_graph.getLayoutConst(m_add->getNodeId()).boundingBox;
    openMenu(m_addMenu.get(), b.x + b.width, b.y + b.height);
}

void JPJobPanel::addBoard(const std::string& path, const char* errorTitle) {
    JPJob* j = m_job();
    if (!j) return;
    std::string error;
    const auto board = m_config.board(path, error);
    if (!board) {
        JDialog::message(errorTitle, error);
        return;
    }
    auto l = std::make_unique<JPBoardLocation>();
    l->holder = board->instance();
    l->fileName = board->file;
    l->parent = &j->root();
    JPPlacementsHolderLocation* added = j->addBoardOrPanelLocation(std::move(l));
    refresh();
    m_table->selectRow(m_model.rowOf(added));
    changed();
}

void JPJobPanel::addPanel(const std::string& path, const char* errorTitle) {
    JPJob* j = m_job();
    if (!j) return;
    std::string error;
    const auto panel = m_config.panel(path, error);
    if (!panel) {
        JDialog::message(errorTitle, error);
        return;
    }
    auto l = std::make_unique<JPPanelLocation>();
    l->holder = panel->instance();
    l->fileName = panel->file;
    l->parent = &j->root();
    JPPlacementsHolderLocation* added = j->addBoardOrPanelLocation(std::move(l));
    j->root().setParentsOfAllDescendants();
    refresh();
    m_table->selectRow(m_model.rowOf(added));
    changed();
}

void JPJobPanel::removeSelected() {
    JPJob* j = m_job();
    if (!j) return;
    for (JPPlacementsHolderLocation* l : selections())
        if (l->parent == &j->root()) j->removeBoardOrPanelLocation(l);
    refresh();
    changed();
}

void JPJobPanel::moveTo(Tool tool, bool next) {
    if (!moveTool) return;
    if (next) {
        const auto rows = m_table->selectedRows();
        const int view = rows.empty() ? -1 : m_table->viewIndexOf(rows.front());
        const int to = m_table->modelRowAt(view + 1);
        if (to >= 0) m_table->selectRow(to);
    }
    const auto s = selections();
    if (!s.empty()) moveTool(tool, s.front()->globalLocation());
}

void JPJobPanel::captureCamera() {
    JPJob* j = m_job();
    const auto s = selections();
    if (!j || s.empty() || !toolLocation) return;
    JPPlacementsHolderLocation* l = s.front();
    if (l->parent != &j->root()) {
        JDialog::message("Error", "Can't update the location of a board or panel that is a child of a larger panel.");
        return;
    }
    const auto at = toolLocation(Tool::Camera);
    if (!at) {
        JDialog::message("Error", "The machine is not connected.");
        return;
    }
    // The camera's X, Y and rotation; the board's own Z kept.
    const double z = l->globalLocation().convertToUnits(at->units()).z();
    m_model.edit(l, [&](JPPlacementsHolderLocation& x) { x.setLocation(at->derive(std::nullopt, std::nullopt, z, std::nullopt)); });
    m_table->refresh();
    m_placements->refresh();
}

void JPJobPanel::captureTool() {
    JPJob* j = m_job();
    const auto s = selections();
    if (!j || s.empty() || !toolLocation) return;
    if (s.size() == 1 && s.front()->parent != &j->root()) {
        JDialog::message("Error", "Can't update the location of a board or panel that is a child of a larger panel.");
        return;
    }
    const auto at = toolLocation(Tool::Nozzle);
    if (!at) {
        JDialog::message("Error", "The machine is not connected.");
        return;
    }
    // The tool's Z, on each straight in the job.
    for (JPPlacementsHolderLocation* l : s)
        if (l->parent == &j->root()) {
            const JPLocation g = l->globalLocation();
            const double z = at->convertToUnits(g.units()).z();
            m_model.edit(l, [&](JPPlacementsHolderLocation& x) { x.setLocation(g.derive(std::nullopt, std::nullopt, z, std::nullopt)); });
        }
    m_table->refresh();
}

} // inline namespace jf
