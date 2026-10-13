// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPJobPanel.h"

#include <j/core/FrameTimer.h>

#include "JPIcons.h"
#include "JPTableTools.h"
#include "JPUiParts.h"

#include "model/JPBoardLocation.h"
#include "model/JPPanelLocation.h"
#include "model/JPSides.h"

#include <j/core/Dialog.h>
#include <j/core/JSeparator.h>

#include <algorithm>
#include <array>
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
        endNudge();
        JPBoardLocationProcess::Hooks h;
        auto tool = [](JPBoardLocationProcess::Tool t) {
            return t == JPBoardLocationProcess::Tool::Camera ? Tool::Camera : Tool::Nozzle;
        };
        h.toolLocation = [this, tool](JPBoardLocationProcess::Tool t) {
            return toolLocation ? toolLocation(tool(t)) : std::nullopt;
        };
        h.moveTool = [this, tool](JPBoardLocationProcess::Tool t, const JPLocation& at) { moveToolTo(tool(t), at); };
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
    m_nudgeButton = bar->add(std::make_unique<JPIconButton>(
        graph, "Board Nudge", JPIcons::boardNudge,
        "Board Nudge: while lit, jog the camera onto a part and the board moves by what you jog (Apply), so every "
        "placement follows; Cancel leaves it as it was. The next fiducial check sets the board's location again."));
    m_nudgeButton->setCheckable(true);
    m_nudgeButton->onToggled.connect([this](bool on) { nudge(on); });
    m_nudge = std::make_unique<JPBoardNudge>(JPBoardNudge::Hooks {
        [this] { return toolLocation ? toolLocation(Tool::Camera) : std::nullopt; },
        [this](const std::string& text) {
            showInstructions("Board Nudge", text, "Apply", [this] { m_nudge->cancel(); }, [this] { m_nudge->apply(); });
        },
        [this] {
            hideInstructions();
            m_nudged = nullptr;
        },
        [this](double dx, double dy) {
            JPPlacementsHolderLocation* l = m_nudged;
            if (!l) return;
            const JPLocation at = l->location().convertToUnits(JPLengthUnit::Millimeters);
            const JPLocation moved = at.derive(at.x() + dx, at.y() + dy, std::nullopt, std::nullopt).convertToUnits(l->location().units());
            m_model.edit(l, [&](JPPlacementsHolderLocation& x) { x.setLocation(moved); });
            m_table->refresh();
            m_placements->refresh();
        },
        [this](bool on) { m_nudgeButton->setChecked(on); } });
    m_measureBoardZ = bar->add(std::make_unique<JPIconButton>(
        graph, "Measure Board Z", JPIcons::boardHeight,
        "Measure Board Z? (Machine Setup, Vision): a fiducial check also measures the board's height at each of its "
        "fiducials and sets its Z from them. On while lit."));
    m_measureBoardZ->setCheckable(true);
    m_measureBoardZ->onToggled.connect([this](bool on) {
        if (setMeasureBoardZ) setMeasureBoardZ(on);
    });
    m_photograph = bar->add(std::make_unique<JPIconButton>(graph, "Photograph Board", JPIcons::photograph,
                                                           "Photograph the selected board(s) with the head camera: kept "
                                                           "beside the board's file and shown in the Board Viewer. Stop "
                                                           "stops it."));
    m_photograph->onClicked.connect([this] {
        const auto s = selections();
        if (!s.empty() && onPhotograph) onPhotograph(s);
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
    JPTableTools* tools = JPUiParts::tableTools(graph, *bar, &openMenu);
    boards->add(std::move(bar));
    m_table = boards->add(std::make_unique<JPTable>(graph));
    m_table->setStateKey("job.boards");
    m_table->setModel(&m_model);
    tools->attach(*m_table);
    m_table->openMenu = [this](JMenu* m, float x, float y) { if (openMenu) openMenu(m, x, y); };
    m_table->onSelectionChanged.connect([this] {
        selectionChanged();
        const auto s = selections();
        if (s.size() == 1 && onLocationChosen) onLocationChosen(*s.front());
    });
    m_table->onEditRefused = [](const std::string&) {};
    m_boardsPane->add(std::move(boards));

    // Below: the chosen one's placements in the job, and the board (or panel) itself.
    m_placementsPane = std::make_unique<JContainer>(graph, 0.f, 0.f);
    m_placementsPane->setDirection(JFlexDirection::Column)->setAlignItems(JAlignItems::Stretch);
    m_lower = m_placementsPane->add(std::make_unique<JTabWidget>(graph, 0.f, 0.f));
    m_lower->setVSizePolicy(JSizePolicyMode::Expanding, 1);
    m_lower->setStripThickness(0);   // a page, not tabs: the chosen one's detail
    m_inJobPage = std::make_unique<JContainer>(graph, 0.f, 0.f);
    m_inJobPage->setDirection(JFlexDirection::Column)->setAlignItems(JAlignItems::Stretch);
    m_placements = m_inJobPage->add(std::make_unique<JPJobPlacementsPanel>(graph, config, job));
    m_placements->setVSizePolicy(JSizePolicyMode::Expanding, 1);
    m_panelPage = std::make_unique<JContainer>(graph, 0.f, 0.f);
    m_panelPage->setDirection(JFlexDirection::Column)->setAlignItems(JAlignItems::Stretch);
    auto constJob = [job]() -> const JPJob* { return job(); };
    // An import, a revision shown: the board itself changed.
    m_placements->boardTools().onChanged = [this] {
        m_placements->refresh();
        m_table->refresh();
        if (onDefinitionChanged) onDefinitionChanged();
    };
    m_panel = m_panelPage->add(std::make_unique<JPPanelDefinitionPanel>(graph, config, constJob));
    m_panel->setVSizePolicy(JSizePolicyMode::Expanding, 1);
    m_panel->openMenu = [this](JMenu* m, float x, float y) { if (openMenu) openMenu(m, x, y); };
    m_panel->onChanged = [this] {
        if (JPJob* j = m_job()) j->root().setParentsOfAllDescendants();
        refresh();
        if (onDefinitionChanged) onDefinitionChanged();
    };
    m_lower->addTab("", m_inJobPage.get());
    m_lower->setActiveTab(0);
    m_placements->openMenu = [this](JMenu* m, float x, float y) { if (openMenu) openMenu(m, x, y); };
    m_placements->toolLocation = [this](Tool t) { return toolLocation ? toolLocation(t) : std::nullopt; };
    m_placements->moveTool = [this](Tool t, const JPLocation& at) {
        moveToolTo(t, at);
    };
    m_placements->onChanged = [this] {
        m_table->refresh();
        changed();
    };
    m_placements->onLibraryChanged = [this] {
        if (onLibraryChanged) onLibraryChanged();
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
    // OpenPnP's descriptions, as its menu entries' tooltips.
    auto sub = [&](const std::string& title, const std::string& tip) {
        m_subMenus.push_back(std::make_unique<JMenu>(title));
        m_menu->add(g, title, {}, m_subMenus.back().get())->setTooltip(tip);
        return m_subMenus.back().get();
    };
    JMenu* side = sub("Set Side", "Set the selected board(s) and/or panel(s) Side");
    for (JPSide s : { JPSide::Bottom, JPSide::Top })
        side->add(g, JPSides::name(s))->onTriggered.connect([this, s] {
            // Only those straight in the job are turned over.
            JPJob* j = m_job();
            if (!j) return;
            std::vector<JPPlacementsHolderLocation*> ls;
            for (JPPlacementsHolderLocation* l : selections())
                if (l->parent == &j->root()) ls.push_back(l);
            m_model.setSides(ls, s);
            m_table->refresh();
            m_placements->refresh();
        });
    JMenu* enabled = sub("Set Enabled", "Set the selected board(s) and/or panel(s) Enabled state");
    for (bool on : { true, false })
        enabled->add(g, on ? "Enabled" : "Disabled")->onTriggered.connect([this, on] {
            std::vector<JPPlacementsHolderLocation*> ls;
            for (JPPlacementsHolderLocation* l : selections())
                if (l->isParentBranchEnabled()) ls.push_back(l);
            m_model.editEach(ls, [on](JPPlacementsHolderLocation& x) { x.locallyEnabled = on; });
            m_table->refresh();
        });
    JMenu* fids = sub("Set Check Fids", "Set the selected board(s) and/or panel(s) Check Fids state");
    for (bool check : { true, false })
        fids->add(g, check ? "Check" : "Don't Check")->onTriggered.connect([this, check] {
            m_model.editEach(selections(), [check](JPPlacementsHolderLocation& x) { x.checkFiducials = check; });
            m_table->refresh();
        });
}

void JPJobPanel::machineChanged() {
    const std::optional<bool> on = measuresBoardZ ? measuresBoardZ() : std::nullopt;
    m_measureBoardZ->setChecked(on.value_or(false));
    m_measureBoardZ->setEnabled(on.has_value());
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
    // The board's tools and the panel itself shown again (changed elsewhere, or another job's).
    m_placements->boardTools().refresh();
    m_panel->refresh();
    updateJobActions();
    machineChanged();
}

std::vector<JPPlacementsHolderLocation*> JPJobPanel::selections() const {
    std::vector<JPPlacementsHolderLocation*> out;
    for (const int r : m_table->selectedRows())
        if (JPPlacementsHolderLocation* l = m_model.location(r)) out.push_back(l);
    return out;
}

void JPJobPanel::selectionChanged() {
    // Another board chosen: the nudge was for the one before.
    if (m_nudged && (selections().size() != 1 || selections().front() != m_nudged)) endNudge();
    showDefinition();
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
    if (m_captureToolItem) m_captureToolItem->setEnabled(m_captureTool->isEnabled());
    if (m_removeItem) m_removeItem->setEnabled(m_remove->isEnabled());
    m_captureCamera->setEnabled(singleTop);
    for (JPIconButton* b : { m_cameraTo, m_cameraNext, m_toolTo, m_fiducialCheck, m_twoPoint })
        b->setEnabled(single || singleTop);
    m_nudgeButton->setEnabled(singleTop || m_nudge->active());
    m_photograph->setEnabled(any && !m_photographing);
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

void JPJobPanel::selectLocation(const JPPlacementsHolderLocation* definition) {
    if (!definition) {
        m_table->clearSelection();
        return;
    }
    for (int r = 0; r < m_model.rowCount(); ++r)
        if (const JPPlacementsHolderLocation* l = m_model.location(r); l && l->definition() == definition->definition()) {
            m_table->selectRow(r);
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

void JPJobPanel::addRunButtons(JToolBar& bar) {
    auto copy = [&](const char* name, JPIconButton* of, const std::function<void()>* action) {
        auto b = std::make_unique<JPIconButton>(m_graph, name, of->icon(), of->tooltip());
        b->onClicked.connect([action] {
            if (*action) (*action)();
        });
        bar.addWidget(b.get());
        return b;
    };
    bar.addSeparator();
    m_barStart = copy("Start", m_start, &onStartPauseResume);
    m_barStep = copy("Step", m_step, &onStep);
    m_barStop = copy("Stop", m_stop, &onStop);
    updateJobActions();
}

void JPJobPanel::showDefinition() {
    const auto s = selections();
    const JPPlacementsHolderLocation* l = s.size() == 1 ? s.front() : nullptr;
    const bool panel = l && l->kind() == JPPlacementsHolderLocation::Kind::Panel;
    std::shared_ptr<JPPanel> shownPanel;
    if (panel && l->holder)
        for (const auto& p : m_config.panels())
            if (p.get() == l->holder->definition()) shownPanel = p;
    // The detail of what is chosen: a panel's definition, else (a board, or nothing) the Placements.
    if (panel != m_panelShown) {
        m_lower->removeTab(0);
        m_lower->addTab("", panel ? m_panelPage.get() : m_inJobPage.get());
        m_lower->setActiveTab(0);
        m_panelShown = panel;
    }
    JPBoard* board = m_placements->boardTools().board();
    if (board != m_shownBoard) {
        m_shownBoard = board;
        if (onBoardShown) onBoardShown(board);
    }
    if (m_panel->panel() != shownPanel.get()) {
        m_panel->setPanel(shownPanel);
        if (onPanelShown) onPanelShown(shownPanel.get());
    }
}

void JPJobPanel::copyChosen() {
    const auto s = selections();
    if (s.size() != 1 || !s.front()->holder) return;
    JPPlacementsHolder* from = const_cast<JPPlacementsHolder*>(s.front()->holder->definition());
    const bool panel = s.front()->kind() == JPPlacementsHolderLocation::Kind::Panel;
    auto copy = [this, from, panel] {
        const std::string suffix = panel ? JPPanel::kExtension : JPBoard::kExtension;
        JDialog::saveFile(panel ? "Save Copy of Panel As..." : "Save Copy of Board As...", { suffix.substr(1) },
                          [this, from, panel, suffix](std::string chosen) {
            const std::string path = JPConfiguration::canonical(withSuffix(chosen, suffix.c_str()));
            std::string error;
            bool saved = false;
            if (!panel) {
                auto b = std::make_shared<JPBoard>(*static_cast<const JPBoard*>(from));
                b->makeDefinition();
                b->ownParts();   // its parts its own, not the board's it was copied from
                b->file = path;
                b->convertedFrom.clear();   // OpenPnP's file is the original's
                b->name = std::filesystem::path(path).filename().string();
                b->dirty = false;
                saved = m_config.saveBoard(*b, error);
                if (saved) {
                    m_config.addBoard(b);
                    addBoard(path, "Unable to copy the board");
                }
            } else {
                auto p = std::make_shared<JPPanel>(*static_cast<const JPPanel*>(from));
                p->makeDefinition();
                for (auto& c : p->children) c->makeDefinition();   // its children its own
                p->file = path;
                p->convertedFrom.clear();
                p->name = std::filesystem::path(path).filename().string();
                p->dirty = false;
                saved = m_config.savePanel(*p, error);
                if (saved) {
                    m_config.addPanel(p);
                    addPanel(path, "Unable to copy the panel");
                }
            }
            if (!saved) JDialog::message(panel ? "Unable to copy the panel" : "Unable to copy the board", error);
        });
    };
    // A copy of what is saved: one with changes asked about first.
    if (from->dirty && confirmSave) confirmSave(*from, copy);
    else copy();
}

void JPJobPanel::setPhotographing(bool on) {
    m_photographing = on;
    updateJobActions();
    m_photograph->setEnabled(!on && !selections().empty());
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
    m_stop->setEnabled(m_machineEnabled && (running || m_runState == RunState::Paused || m_photographing));
    if (m_startItem) {
        m_startItem->setLabel(running ? "Pause" : m_runState == RunState::Paused ? "Resume" : "Start");
        m_startItem->setTooltip(m_start->tooltip());
        m_startItem->setEnabled(m_start->isEnabled());
    }
    if (m_stepItem) m_stepItem->setEnabled(m_step->isEnabled());
    if (m_stopItem) m_stopItem->setEnabled(m_stop->isEnabled());
    const std::array<std::pair<JPIconButton*, JPIconButton*>, 3> copies { { { m_barStart.get(), m_start },
                                                                            { m_barStep.get(), m_step },
                                                                            { m_barStop.get(), m_stop } } };
    for (const auto& [bar, of] : copies)
        if (bar) {
            bar->setIcon(of->icon());
            bar->setTooltip(of->tooltip());
            bar->setEnabled(of->isEnabled());
        }
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
    auto entry = [&](const std::string& label, const char* tip, std::function<void()> act) {
        JMenuItem* item = m_addMenu->add(g, label);
        item->setTooltip(tip);
        item->onTriggered.connect(std::move(act));
        return item;
    };
    entry("New Board...", "Create a new board and add it to the job.", [this] { addNew(false); });
    entry("Existing Board...", "Add an existing board to the job.", [this] { addExisting(false); });
    m_addMenu->addSeparator(g);
    entry("New Panel...", "Create a new panel and add it to the job.", [this] { addNew(true); });
    entry("Existing Panel...", "Add an existing panel to the job.", [this] { addExisting(true); });
    // A copy of the one chosen, as a file of its own.
    const auto s = selections();
    const bool panel = s.size() == 1 && s.front()->kind() == JPPlacementsHolderLocation::Kind::Panel;
    m_addMenu->addSeparator(g);
    entry(panel ? "Copy of the Chosen Panel..." : "Copy of the Chosen Board...",
          "Save a copy of the chosen board or panel as a new file, and add the copy to the job.", [this] { copyChosen(); })
        ->setEnabled(s.size() == 1);
    const JRect b = m_graph.getLayoutConst(m_add->getNodeId()).boundingBox;
    openMenu(m_addMenu.get(), b.x + b.width, b.y + b.height);
}

void JPJobPanel::addNew(bool panel) {
    if (panel)
        JDialog::saveFile("Save New Panel As...", { "jppanel" }, [this](std::string path) {
            addPanel(withSuffix(path, JPPanel::kExtension), "Unable to create new panel");
        });
    else
        JDialog::saveFile("Save New Board As...", { "jpboard" }, [this](std::string path) {
            addBoard(withSuffix(path, JPBoard::kExtension), "Unable to create new board");
        });
}

void JPJobPanel::addExisting(bool panel) {
    if (!chooseExisting) return;
    if (panel) chooseExisting("Add existing panel to job", "panel", [this](std::string path) { addPanel(path, "Panel load failed"); });
    else chooseExisting("Add existing board to job", "board", [this](std::string path) { addBoard(path, "Board load failed"); });
}

void JPJobPanel::setEditItems(JMenuItem* remove, JMenuItem* captureTool) {
    m_removeItem = remove;
    m_captureToolItem = captureTool;
    selectionChanged();
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
    if (defaultLocation) l->setLocation(defaultLocation());
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
    if (defaultLocation) l->setLocation(defaultLocation());
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
    if (!s.empty()) moveToolTo(tool, s.front()->globalLocation());
}

void JPJobPanel::moveToolTo(Tool tool, const JPLocation& at) {
    endNudge();
    if (moveTool) moveTool(tool, at);
}

void JPJobPanel::nudge(bool on) {
    if (!on) {
        m_nudge->apply();
        return;
    }
    JPJob* j = m_job();
    const auto s = selections();
    std::string why = "Choose one board or panel of the job (not one inside a panel) to nudge.";
    if (!j || s.size() != 1 || s.front()->parent != &j->root() || m_locating || !m_nudge->start(why)) {
        m_nudgeButton->setChecked(false);
        if (!m_locating) JDialog::message("Board Nudge", why);
        return;
    }
    m_nudged = s.front();
}

void JPJobPanel::endNudge() {
    m_nudge->cancel();
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
