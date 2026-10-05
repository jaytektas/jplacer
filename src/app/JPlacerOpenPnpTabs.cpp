// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerOpenPnpTabs.h"

#include "JPlacerChildFiducialsDialog.h"
#include "JPlacerClassSelectionDialog.h"
#include "JPlacerChoiceDialog.h"
#include "JPlacerExistingHolderDialog.h"
#include "JPlacerPanelArrayDialog.h"
#include "JPlacerPhotonSlotsDialog.h"
#include "JPlacerImportDialog.h"
#include "JPlacerSettings.h"
#include <sstream>
#include <set>
#include <j/core/FrameTimer.h>
#include <j/platform/JDesktop.h>
#include "setup/JPIssueChecks.h"

#include "model/JPDefinitionChanges.h"
#include "tasks/JPFeederActions.h"
#include "tasks/JPFeederFeed.h"
#include "tasks/JPFeederPipelines.h"

#include "ui/JPFootprintOverlay.h"

#include <j/config/Settings.h>
#include <j/core/Dialog.h>
#include <j/core/JTextHelper.h>
#include <j/core/MenuSystem.h>

#include <filesystem>

inline namespace jf {

namespace {
constexpr double kSplit = 0.5;   // a table's share before its divider is moved
// The cameras' overlay for a package's footprint (OpenPnP's reticle key).
constexpr const char* kFootprintOverlay = "PackageVisionWizard";
}

JPlacerOpenPnpTabs::JPlacerOpenPnpTabs(JAppWindow& window, JSceneGraph& graph, JPlacerJob& job, JPlacerMachine& machine)
    : m_window(window), m_job(job), m_machine(machine), m_layout(machine.layout()), m_pipelines(window, machine) {
    auto openMenu = [this](JMenu* menu, float x, float y) {
        if (JMenuManager::instance().onOpenMenu)
            JMenuManager::instance().onOpenMenu(menu, m_window.windowX() + int(x), m_window.windowY() + int(y), false, false);
    };

    auto currentJob = [this]() -> const JPJob* { return &m_job.job(); };
    m_boards = std::make_unique<JPBoardsPanel>(graph, job.configuration(), currentJob,
                                               JSettings::instance().get<double>(JPlacerSettings::kBoardsSplit, kSplit));
    m_boards->openMenu = openMenu;
    m_boards->onChanged = [this] {
        if (m_boardViewer) m_boardViewer->regenerate();
        changed();
    };
    m_boards->confirmSave = [this](JPPlacementsHolder& h, std::function<void()> then) { confirmSave(h, std::move(then)); };
    JPBoardPlacementsPanel& placements = m_boards->placements();
    placements.openImporter = [this](const JPBoardImporter& importer, std::function<void(JPBoard&)> imported) {
        m_window.openModal<JPlacerImportDialog>(importer, m_job.configuration(), std::move(imported));
    };
    placements.askChoice = [this](const std::string& title, const std::string& question, std::vector<std::string> options,
                                  int cancelIndex, std::function<void(int)> chosen) {
        m_window.openModal<JPlacerChoiceDialog>(title, question, std::move(options), cancelIndex, std::move(chosen));
    };
    // The board viewer: a placement turned on or off there is turned so on the board.
    m_boardViewer = std::make_unique<JPlacerViewerDock>(graph, m_layout, "Board");
    m_boardViewer->canvas().onPlacementEnabled = [this, currentJob](JPPlacementsHolderLocation* where,
                                                                    const std::string& id, bool on) {
        if (!where || !where->holder) return;
        JPDefinitionChanges(m_job.configuration(), currentJob()).placement(*where->holder, id, [on](JPPlacement& p) {
            p.enabled = on;
        });
        m_boards->refresh();
        m_boardViewer->regenerate();
        changed();
    };
    placements.onViewBoard = [this] { m_boardViewer->open(boardOf(m_boards->placements().board())); };
    m_boards->onBoardShown = [this](JPBoard* b) { m_boardViewer->follow(boardOf(b)); };
    m_boardsDock = std::make_unique<JDockWidget>("Boards", 0.f, 0.f, 0.f, 0.f);
    m_boardsDock->setContent(m_boards.get());
    m_layout.add(m_boardsDock.get(), JPlacerLayout::Home::Work);

    // Job: the job's boards and panels and their placements, the machine's
    // moves, the job viewer and the placements done in the status line.
    auto chooseExisting = [this](const std::string& title, const std::string& what, std::function<void(std::string)> chosen) {
        std::vector<std::string> files;
        if (what == "board")
            for (const auto& b : m_job.configuration().boards()) files.push_back(b->file);
        else
            for (const auto& p : m_job.configuration().panels()) files.push_back(p->file);
        m_window.openModal<JPlacerExistingHolderDialog>(title, what, files, std::move(chosen));
    };
    auto setupTool = [](JPJobPanel::Tool t) {
        return t == JPJobPanel::Tool::Camera ? JPSetupForm::Tool::Camera : JPSetupForm::Tool::Nozzle;
    };
    m_jobPanel = std::make_unique<JPJobPanel>(graph, job.configuration(), [this] { return &m_job.job(); },
                                              JSettings::instance().get<double>(JPlacerSettings::kJobSplit, kSplit));
    m_jobPanel->openMenu = openMenu;
    m_jobPanel->onChanged = [this] {
        if (m_jobViewer) m_jobViewer->regenerate();
        m_job.changed();
    };
    m_jobPanel->toolLocation = [this, setupTool](JPJobPanel::Tool t) { return m_machine.toolLocation(setupTool(t)); };
    m_jobPanel->moveTool = [this, setupTool](JPJobPanel::Tool t, const JPLocation& at) { m_machine.moveToolTo(setupTool(t), at); };
    m_jobPanel->chooseExisting = chooseExisting;
    m_jobViewer = std::make_unique<JPlacerViewerDock>(graph, m_layout, "Job");
    auto jobName = [this] {
        return m_job.job().file.empty() ? std::string(JPlacerJob::kUntitled)
                                        : std::filesystem::path(m_job.job().file).filename().string();
    };
    m_jobPanel->onViewJob = [this, jobName](std::vector<const JPPlacementsHolderLocation*> chosen) {
        m_jobViewer->openJob(&m_job.job().root(), jobName(), std::move(chosen));
    };
    m_jobPanel->onSelectionChanged = [this, jobName](std::vector<const JPPlacementsHolderLocation*> chosen) {
        m_jobViewer->followJob(&m_job.job().root(), jobName(), std::move(chosen));
    };
    m_placedLabel = std::make_unique<JLabel>(graph, "Placements: 0 / 0 Total | 0 / 0 Selected Board ", 0.f);
    m_placedBar = std::make_unique<JProgressBar>(graph);
    m_window.statusBar().addWidget(m_placedLabel.get(),
                                   JTextHelper::measureWidth("Placements: 0000 / 0000 Total | 0000 / 0000 Selected Board "));
    m_window.statusBar().addWidget(m_placedBar.get(), JTextHelper::measureWidth("0000000000"));
    m_jobPanel->placements().onCompletion = [this](int placed, int total, int boardPlaced, int boardTotal) {
        m_placedLabel->setText("Placements: " + std::to_string(placed) + " / " + std::to_string(total) + " Total | " +
                               std::to_string(boardPlaced) + " / " + std::to_string(boardTotal) + " Selected Board ");
        m_placedBar->setProgress(total > 0 ? float(placed) / float(total) : 0.f);
    };
    m_jobPanel->placements().updateActivePlacements();
    m_jobDock = std::make_unique<JDockWidget>("Job", 0.f, 0.f, 0.f, 0.f);
    m_jobDock->setContent(m_jobPanel.get());
    m_layout.add(m_jobDock.get(), JPlacerLayout::Home::Work);

    // Panels: its definition's dialogs, and the panel viewer.
    m_panels = std::make_unique<JPPanelsPanel>(graph, job.configuration(), currentJob,
                                               JSettings::instance().get<double>(JPlacerSettings::kPanelsSplit, kSplit));
    m_panels->openMenu = openMenu;
    m_panels->confirmSave = [this](JPPlacementsHolder& h, std::function<void()> then) { confirmSave(h, std::move(then)); };
    m_panels->onChanged = [this] {
        if (m_panelViewer) m_panelViewer->regenerate();
        m_boards->refresh();
        changed();
    };
    JPPanelDefinitionPanel& definition = m_panels->definition();
    definition.chooseExisting = chooseExisting;
    definition.openArray = [this, currentJob](JPPanelLocation& panel, JPPlacementsHolderLocation& child,
                                              std::function<void()> done) {
        m_window.openModal<JPlacerPanelArrayDialog>(m_job.configuration(), currentJob(), panel, child, std::move(done));
    };
    definition.chooseChildFiducials = [this](JPPanelLocation& panel, std::function<void(std::vector<std::string>)> chosen) {
        m_window.openModal<JPlacerChildFiducialsDialog>(m_job.configuration(), panel, std::move(chosen));
    };
    m_panelViewer = std::make_unique<JPlacerViewerDock>(graph, m_layout, "Panel");
    auto panelOf = [this](const JPPanel* panel) {
        std::shared_ptr<JPPlacementsHolder> found;
        for (const auto& p : m_job.configuration().panels())
            if (p.get() == panel) found = p;
        return found;
    };
    definition.onViewPanel = [this, panelOf] { m_panelViewer->open(panelOf(m_panels->definition().panel())); };
    m_panels->onPanelShown = [this, panelOf](JPPanel* p) { m_panelViewer->follow(panelOf(p)); };
    // The panel viewer's menu: the panel's direct children on or off, their fiducials checked or not.
    m_panelViewer->canvas().onLocationEnabled = [this, currentJob](JPPlacementsHolderLocation* where, bool on) {
        auto* def = m_panels->definition().panel();
        if (!where || !def) return;
        JPDefinitionChanges(m_job.configuration(), currentJob()).child(*def, where->id, [on](JPPlacementsHolderLocation& c) {
            c.locallyEnabled = on;
        });
        m_panels->refresh();
        m_panelViewer->regenerate();
        changed();
    };
    m_panelViewer->canvas().onCheckFiducials = [this, currentJob](JPPlacementsHolderLocation* where, bool check) {
        auto* def = m_panels->definition().panel();
        if (!where || !def) return;
        JPDefinitionChanges(m_job.configuration(), currentJob()).child(*def, where->id, [check](JPPlacementsHolderLocation& c) {
            c.checkFiducials = check;
        });
        m_panels->refresh();
        m_panelViewer->regenerate();
        changed();
    };
    m_panelViewer->canvas().onPlacementEnabled = [this, currentJob](JPPlacementsHolderLocation* where,
                                                                    const std::string& id, bool on) {
        auto* def = m_panels->definition().panel();
        if (!where || !def) return;
        if (def->find(id)) {
            JPDefinitionChanges(m_job.configuration(), currentJob()).placement(*def, id, [on](JPPlacement& p) {
                p.enabled = on;
            });
        } else if (on) {
            def->disabledPseudoPlacements.erase(id);
        } else {
            def->disabledPseudoPlacements.insert(id);
        }
        m_panels->refresh();
        m_panelViewer->regenerate();
        changed();
    };
    m_panelsDock = std::make_unique<JDockWidget>("Panels", 0.f, 0.f, 0.f, 0.f);
    m_panelsDock->setContent(m_panels.get());
    m_layout.add(m_panelsDock.get(), JPlacerLayout::Home::Work);

    m_parts = std::make_unique<JPPartsPanel>(graph, job.configuration(),
                                             JSettings::instance().get<double>(JPlacerSettings::kPartsSplit, kSplit));
    m_parts->openMenu = openMenu;
    m_parts->onChanged = [this] { m_job.configurationChanged(); };
    m_parts->machineDefaults = [this] { return machineVisionDefaults(); };
    m_parts->editPipeline = [this](const std::string& id, const JPVisionForms::Holder& h) {
        m_pipelines.editVision(m_job.configuration(), id, h.id, "", [this] { m_job.configurationChanged(); });
    };
    m_parts->onParameterChanged = [this] { m_job.configurationKept(); };
    m_parts->previewParameter = [this](const std::string& id, const JPVisionForms::Holder& h, const std::string& parameter) {
        m_pipelines.previewVision(m_job.configuration(), id, h.id, "", parameter);
    };
    m_parts->onPickPart = [this](const JPPart& part) {
        JPFeeder* f = m_job.configuration().findFeeder(part.id, m_machine.toolLocation(JPSetupForm::Tool::Camera));
        if (!f) {
            JDialog::message("Error", "No valid feeder found for " + part.id);
            return;
        }
        m_feeders->pickFrom(*f);
    };
    m_partsDock = std::make_unique<JDockWidget>("Parts", 0.f, 0.f, 0.f, 0.f);
    m_partsDock->setContent(m_parts.get());
    m_layout.add(m_partsDock.get(), JPlacerLayout::Home::Work);

    m_packages = std::make_unique<JPPackagesPanel>(graph, job.configuration(),
                                                   JSettings::instance().get<double>(JPlacerSettings::kPackagesSplit, kSplit));
    m_packages->openMenu = openMenu;
    m_packages->nozzleTips = [this] { return m_machine.nozzleTips(); };
    m_packages->onShowFootprint = [this](const JPFootprint* f) {
        m_machine.setCameraOverlay(kFootprintOverlay, f ? JPFootprintOverlay::of(*f) : nullptr);
    };
    m_packages->onChanged = [this] { m_job.configurationChanged(); };
    m_packages->machineDefaults = [this] { return machineVisionDefaults(); };
    m_packages->editPipeline = [this](const std::string& id, const JPVisionForms::Holder& h) {
        m_pipelines.editVision(m_job.configuration(), id, "", h.id, [this] { m_job.configurationChanged(); });
    };
    m_packages->onParameterChanged = [this] { m_job.configurationKept(); };
    m_packages->previewParameter = [this](const std::string& id, const JPVisionForms::Holder& h, const std::string& parameter) {
        m_pipelines.previewVision(m_job.configuration(), id, "", h.id, parameter);
    };
    m_packagesDock = std::make_unique<JDockWidget>("Packages", 0.f, 0.f, 0.f, 0.f);
    m_packagesDock->setContent(m_packages.get());
    m_layout.add(m_packagesDock.get(), JPlacerLayout::Home::Work);

    // Vision: the vision settings; the machine's defaults from the cell.
    m_vision = std::make_unique<JPVisionSettingsPanel>(graph, job.configuration(),
                                                       JSettings::instance().get<double>(JPlacerSettings::kVisionSplit, kSplit));
    m_vision->onChanged = [this] { m_job.configurationChanged(); };
    m_vision->machineDefaults = [this] { return machineVisionDefaults(); };
    m_vision->editPipeline = [this](const std::string& id, const JPVisionForms::Holder&) {
        m_pipelines.editVision(m_job.configuration(), id, "", "", [this] { m_job.configurationChanged(); });
    };
    m_vision->onParameterChanged = [this] { m_job.configurationKept(); };
    m_vision->previewParameter = [this](const std::string& id, const JPVisionForms::Holder&, const std::string& parameter) {
        m_pipelines.previewVision(m_job.configuration(), id, "", "", parameter);
    };
    m_pipelines.chosenPart = [this] {
        const JPPart* p = m_parts->selectedPart();
        return p ? p->id : std::string();
    };
    m_pipelines.chosenPackage = [this] {
        const JPPackage* p = m_packages->selectedPackage();
        return p ? p->id : std::string();
    };
    m_visionDock = std::make_unique<JDockWidget>("Vision", 0.f, 0.f, 0.f, 0.f);
    m_visionDock->setContent(m_vision.get());
    m_layout.add(m_visionDock.get(), JPlacerLayout::Home::Work);

    // Feeders: the machine's moves and picks, and OpenPnP's machine's feeders taken on its import.
    m_feeders = std::make_unique<JPFeedersPanel>(graph, job.configuration(),
                                                 JSettings::instance().get<double>(JPlacerSettings::kFeedersSplit, kSplit));
    m_feeders->openMenu = openMenu;
    m_feeders->onChanged = [this] {
        m_job.configurationChanged();
        ensurePhotonActuator();
    };
    m_feeders->chooseClass = [this](const std::string& title, const std::string& description,
                                    const std::vector<std::string>& classes, std::function<void(std::string)> chosen) {
        m_window.openModal<JPlacerClassSelectionDialog>(title, description, classes, std::move(chosen));
    };
    m_feeders->whereIs = [this](JPFeedersPanel::Tool t) { return m_machine.whereIs(t); };
    m_feeders->moveTo = [this](JPFeedersPanel::Tool t, const JPFeedersPanel::Where& to) { m_machine.moveToolTo(t, to); };
    m_feeders->whereIsActuator = [this](const std::string& name) { return m_machine.whereIsActuator(name); };
    m_feeders->moveActuatorTo = [this](const std::string& name, const JPFeedersPanel::Where& to) {
        m_machine.moveActuatorTo(name, to);
    };
    m_feeders->cameraView = [this] { return m_machine.headCameraView(); };
    m_feeders->setHal(&m_window.hal());
    m_feeders->machineFeed = [this](const std::string& feederId, bool pick) {
        const std::string nozzle = m_machine.chosenNozzleId();
        m_jobRun->machineTask([this, feederId, pick, nozzle](JPJobMachine& machine,
                                                              const std::function<void(const std::function<void()>&)>& onMain,
                                                              std::string& why) {
            bool empty = false;
            if (!JPFeederFeed::feed(m_job.configuration(), feederId, nozzle, machine, onMain, why, empty)) return false;
            if (!pick) return true;
            std::optional<JPLocation> at;
            std::string kind;
            onMain([&] {
                if (const JPFeeder* f = m_job.configuration().feeder(feederId)) {
                    at = f->pickLocation();
                    kind = f->typeName();
                }
            });
            if (!at) {
                why = "jplacer does not work out where a " + kind + " picks yet.";
                return false;
            }
            if (nozzle.empty()) {
                why = "No nozzle to pick with";
                return false;
            }
            return machine.safeZ(why) && machine.pick(nozzle, *at, why)
                && JPFeederFeed::postPick(m_job.configuration(), feederId, machine, onMain, why) && machine.safeZ(why);
        });
    };
    m_feeders->actuatorNames = [this] {
        std::vector<std::string> out;
        if (const JPCell* c = m_machine.cell())
            for (const JPActuatorConfig& a : c->config().actuators) out.push_back(a.name.empty() ? a.id : a.name);
        return out;
    };
    m_feeders->machineAction = [this](const std::string& feederId, const std::string& action) {
        if (action.rfind("photon", 0) == 0) ensurePhotonActuator();
        const bool started = m_jobRun->machineTask([this, feederId, action](JPJobMachine& machine,
                                                       const std::function<void(const std::function<void()>&)>& onMain,
                                                       std::string& why) {
            JPFeederActions::Outcome outcome;
            std::string fiducialVision;
            onMain([&] { fiducialVision = machineVisionDefaults().second; });
            const bool ok = JPFeederActions::run(m_job.configuration(), feederId, action, machine, onMain, fiducialVision,
                                                 outcome, why, [this, &onMain](int address, int state) {
                                                     onMain([&] { m_feeders->showSearchState(address, state); });
                                                 });
            onMain([&] {
                for (const auto& [key, value] : outcome.readings) m_feeders->showReading(feederId, key, value);
                if (outcome.changed) {
                    m_feeders->refresh();
                    m_job.configurationChanged();
                }
                if (action == "photonSearch") m_feeders->searchEnded();
            });
            return ok;
        });
        // Refused: a search never began.
        if (!started && action == "photonSearch") m_feeders->searchEnded();
    };
    m_feeders->machineReady = [this] { return m_machine.cell() && m_machine.cell()->isConnected(); };
    m_feeders->programPhotonSlots = [this] {
        if (!m_machine.cell() || !m_machine.cell()->isConnected()) {
            JDialog::message("Error", "Please connect to the machine before running this wizard.");
            return;
        }
        ensurePhotonActuator();
        m_window.openModal<JPlacerPhotonSlotsDialog>(m_job.configuration(), [this](JPlacerPhotonSlotsDialog::Work work) {
            return m_jobRun->machineTask(std::move(work));
        });
    };
    // A strip feeder's pipeline: edited on the head camera, or its default back.
    m_feeders->pipelineAction = [this](const std::string& feederId, const std::string& action) {
        JPFeeder* f = m_job.configuration().feeder(feederId);
        if (!f) return;
        if (action == "resetPipeline") {
            if (JPFeederPipelines::reset(*f)) m_job.configurationChanged();
            return;
        }
        std::optional<JPPipeline> held = JPFeederPipelines::of(*f);
        if (!held) return;
        auto pipeline = std::make_shared<JPPipeline>(std::move(*held));
        m_pipelines.useHeadCamera(*pipeline, m_job.configuration().directory());
        JPFeederPipelines::configureForEditing(*f, *pipeline);
        m_pipelines.edit(f->name() + " Pipeline", pipeline, [this, feederId](const JPPipeline& edited) {
            if (JPFeeder* kept = m_job.configuration().feeder(feederId)) {
                kept->setPipeline(edited.toXml());
                m_job.configurationChanged();
            }
        });
    };
    m_feeders->partUsed = [this](const std::string& partId) {
        for (const JPBoardLocation* l : m_job.job().boardLocations()) {
            if (!l->isEnabled() || !l->holder) continue;
            for (const JPPlacement& p : l->holder->placements)
                if (p.type == JPPlacement::Type::Placement && p.enabled && p.partId == partId) return true;
        }
        return false;
    };
    m_feedersDock = std::make_unique<JDockWidget>("Feeders", 0.f, 0.f, 0.f, 0.f);
    m_feedersDock->setContent(m_feeders.get());
    m_layout.add(m_feedersDock.get(), JPlacerLayout::Home::Work);
    // Issues & Solutions: the checks over the machine and the configuration,
    // the milestone and what was solved or dismissed kept between sessions.
    {
        JSettings& set = JSettings::instance();
        const std::string target = set.get<std::string>(JPlacerSettings::kIssuesMilestone, "Welcome");
        for (int m = 0; m <= int(JPSolutions::Milestone::Advanced); ++m)
            if (target == JPSolutions::name(JPSolutions::Milestone(m))) m_solutions.setTargetMilestone(JPSolutions::Milestone(m));
        auto words = [](const std::string& text) {
            std::set<std::string> out;
            std::istringstream in(text);
            for (std::string w; in >> w;) out.insert(w);
            return out;
        };
        m_solutions.setFingerprints(words(set.get<std::string>(JPlacerSettings::kIssuesSolved, "")),
                                    words(set.get<std::string>(JPlacerSettings::kIssuesDismissed, "")));
        m_solutions.setShowSolved(set.get<bool>(JPlacerSettings::kIssuesShowSolved, false));
        m_solutions.setShowDismissed(set.get<bool>(JPlacerSettings::kIssuesShowDismissed, false));
    }
    m_solutions.onChanged = [this] {
        auto joined = [](const std::set<std::string>& words) {
            std::string out;
            for (const std::string& w : words) out += (out.empty() ? "" : " ") + w;
            return out;
        };
        JSettings& set = JSettings::instance();
        set.set(JPlacerSettings::kIssuesMilestone, std::string(JPSolutions::name(m_solutions.targetMilestone())));
        set.set(JPlacerSettings::kIssuesSolved, joined(m_solutions.solvedFingerprints()));
        set.set(JPlacerSettings::kIssuesDismissed, joined(m_solutions.dismissedFingerprints()));
        set.set(JPlacerSettings::kIssuesShowSolved, m_solutions.showSolved());
        set.set(JPlacerSettings::kIssuesShowDismissed, m_solutions.showDismissed());
        JPlacerSettings::save();
    };
    m_solutions.confirm = [](const std::string& message, std::function<void()> yes) {
        JDialog::confirm("Warning", message, std::move(yes));
    };
    {
        JPIssueChecks::Context context;
        context.config = &m_job.configuration();
        context.cell = [this]() -> const JPCellConfig* { return m_machine.cell() ? &m_machine.cell()->config() : nullptr; };
        context.calibrated = [this](const std::string& id) { return m_machine.cameraCalibrated(id); };
        context.showSetup = [this](const std::string& path) { m_machine.showSetupNode(path); };
        context.homed = [this] { return m_machine.cell() && m_machine.cell()->isHomed(); };
        context.home = [this] { m_machine.home(); };
        context.axisPosition = [this](const std::string& axisId) -> std::optional<double> {
            const JPCell* c = m_machine.cell();
            if (!c || !c->isConnected()) return std::nullopt;
            const auto p = c->positions();
            const auto i = p.find(axisId);
            return i == p.end() ? std::nullopt : std::optional(i->second);
        };
        context.changeCell = [this](const std::string& what, const std::function<void(JPCellConfig&)>& edit) {
            m_machine.changeSetup(what, edit);
        };
        m_solutions.setChecks(JPIssueChecks::all(context));
    }
    m_issues = std::make_unique<JPIssuesPanel>(graph, m_solutions,
                                               JSettings::instance().get<double>(JPlacerSettings::kIssuesSplit, kSplit));
    m_issues->openUri = [](const std::string& uri) { JDesktop::openUrl(uri); };
    m_issues->showError = [](const std::string& why) { JDialog::message("Error", why); };
    m_issuesDock = std::make_unique<JDockWidget>("Issues & Solutions", 0.f, 0.f, 0.f, 0.f);
    // A milestone completed: searched again (not while its own Accept is at work).
    m_solutions.onMilestoneChanged = [this, alive = std::weak_ptr<bool>(m_alive)] {
        jPostToNextFrame([this, alive] {
            if (const auto a = alive.lock(); a && *a) m_issues->findIssuesAndSolutions();
        });
    };
    m_issuesDock->setContent(m_issues.get());
    m_layout.add(m_issuesDock.get(), JPlacerLayout::Home::Work);
    // The first search once everything is up (OpenPnP's waits for its cameras too).
    jPostToNextFrame([this, alive = std::weak_ptr<bool>(m_alive)] {
        if (const auto a = alive.lock(); a && *a) m_issues->findIssuesAndSolutions();
    });

    // Log: the log's entries, as OpenPnP's Log tab; its level kept as the Console's is.
    m_log = std::make_unique<JPLogPanel>(graph);
    m_log->onLogLevels = [](const JPLogLevels& levels) {
        JSettings::instance().set(JPlacerSettings::kLogLevels, levels.toText());
        JPlacerSettings::save();
    };
    m_logDock = std::make_unique<JDockWidget>("Log", 0.f, 0.f, 0.f, 0.f);
    m_logDock->setContent(m_log.get());
    m_layout.add(m_logDock.get(), JPlacerLayout::Home::Work);
    m_jobPanel->placements().onEditFeeder = [this](const std::string& partId) {
        m_layout.show(m_feedersDock.get());
        m_feeders->showFeederForPart(partId);
    };
    m_machine.onImported = [this](const std::string& machineXml) {
        std::string error;
        const int n = m_job.configuration().importFeeders(machineXml, error);
        if (n < 0) {
            JDialog::message("Feeders not imported", error);
            return;
        }
        m_job.configurationChanged();
        ensurePhotonActuator();
    };

    // Running the job: a failure's source chosen where it is shown, as OpenPnP does.
    m_jobRun = std::make_unique<JPlacerJobRun>(m_window, job, machine, *m_jobPanel);
    m_jobRun->onPlaced = [this] {
        m_jobPanel->placements().refresh();
        if (m_jobViewer) m_jobViewer->regenerate();
    };
    m_jobRun->showSource = [this](const JPJobProcessor::Failure& f) {
        using Source = JPJobProcessor::Failure::Source;
        switch (f.source) {
            case Source::Board:
            case Source::Placement:
                m_layout.show(m_jobDock.get());
                m_jobPanel->select(f.id, f.placementId);
                break;
            case Source::Part:
                m_layout.show(m_partsDock.get());
                m_parts->selectPart(m_job.configuration().part(f.id));
                break;
            case Source::Feeder:
                m_layout.show(m_feedersDock.get());
                m_feeders->selectFeeder(f.id);
                break;
            default: break;
        }
    };

    // The pages shown again on the next frame, once for all the changes made
    // meanwhile: a change made from a page (a slider dragged) must not take
    // the page away while it is still handling the click.
    m_watch = m_job.watch([this, jobName](JPlacerJob::Change) {
        if (m_refreshPending) return;
        m_refreshPending = true;
        jPostToNextFrame([this, jobName, alive = std::weak_ptr<bool>(m_alive)] {
            if (const auto a = alive.lock(); !a || !*a) return;
            m_refreshPending = false;
            m_jobPanel->refresh();
            m_jobViewer->followJob(&m_job.job().root(), jobName(), {});
            m_panels->refresh();
            m_boards->refresh();
            m_parts->refresh();
            m_packages->refresh();
            m_feeders->refresh();
            m_vision->refresh();
            ensurePhotonActuator();
        });
    });
}

JPlacerOpenPnpTabs::~JPlacerOpenPnpTabs() {
    *m_alive = false;
    m_jobRun.reset();   // a run under way stops before what it works on goes
    JSettings::instance().set(JPlacerSettings::kPartsSplit, m_parts->split());
    JSettings::instance().set(JPlacerSettings::kPackagesSplit, m_packages->split());
    JSettings::instance().set(JPlacerSettings::kFeedersSplit, m_feeders->split());
    JSettings::instance().set(JPlacerSettings::kVisionSplit, m_vision->split());
    JSettings::instance().set(JPlacerSettings::kBoardsSplit, m_boards->split());
    JSettings::instance().set(JPlacerSettings::kPanelsSplit, m_panels->split());
    JSettings::instance().set(JPlacerSettings::kJobSplit, m_jobPanel->split());
    JPlacerSettings::save();
    m_job.unwatch(m_watch);
    m_machine.setCameraOverlay(kFootprintOverlay, nullptr);
    m_layout.remove(m_partsDock.get());
    m_partsDock->setContent(nullptr);
    m_layout.remove(m_packagesDock.get());
    m_packagesDock->setContent(nullptr);
    m_machine.onImported = nullptr;
    m_layout.remove(m_feedersDock.get());
    m_feedersDock->setContent(nullptr);
    m_layout.remove(m_logDock.get());
    m_logDock->setContent(nullptr);
    JSettings::instance().set(JPlacerSettings::kIssuesSplit, m_issues->split());
    m_layout.remove(m_issuesDock.get());
    m_issuesDock->setContent(nullptr);
    m_layout.remove(m_visionDock.get());
    m_visionDock->setContent(nullptr);
    m_layout.remove(m_boardsDock.get());
    m_boardsDock->setContent(nullptr);
    m_boardViewer.reset();
    m_layout.remove(m_panelsDock.get());
    m_panelsDock->setContent(nullptr);
    m_panelViewer.reset();
    m_layout.remove(m_jobDock.get());
    m_jobDock->setContent(nullptr);
    m_jobViewer.reset();
}

std::pair<std::string, std::string> JPlacerOpenPnpTabs::machineVisionDefaults() const {
    JPVisionConfig v;
    if (const JPCell* c = m_machine.cell()) v = c->config().vision;
    return { v.bottomVisionId, v.fiducialVisionId };
}

void JPlacerOpenPnpTabs::changed() {
    m_job.configurationChanged();
}

std::shared_ptr<JPBoard> JPlacerOpenPnpTabs::boardOf(const JPBoard* board) const {
    for (const auto& b : m_job.configuration().boards())
        if (b.get() == board) return b;
    return nullptr;
}

const std::vector<std::unique_ptr<JPBoardImporter>>& JPlacerOpenPnpTabs::importers() const {
    return m_boards->placements().importers();
}

void JPlacerOpenPnpTabs::importBoard(const JPBoardImporter& importer) {
    m_boards->placements().importBoard(importer);
}

void JPlacerOpenPnpTabs::confirmSave(JPPlacementsHolder& holder, std::function<void()> then) {
    const std::string name = std::filesystem::path(holder.file).filename().string();
    const std::string file = holder.file;
    m_window.openModal<JPlacerChoiceDialog>(
        "Save " + name + "?", "Do you want to save your changes to " + name + "?\nIf you don't save, your changes will be lost.",
        std::vector<std::string> { "Yes", "No", "Cancel" }, 2, [this, file, then](int choice) {
            if (choice == 0) {
                std::string error;
                for (const auto& b : m_job.configuration().boards())
                    if (b->file == file && !m_job.configuration().saveBoard(*b, error))
                        JDialog::message("Save Error", error);
                for (const auto& p : m_job.configuration().panels())
                    if (p->file == file && !m_job.configuration().savePanel(*p, error))
                        JDialog::message("Save Error", error);
            }
            if (then) then();
        });
}

void JPlacerOpenPnpTabs::confirmSaveAll(std::vector<std::string> files, std::function<void()> then) {
    JPConfiguration& config = m_job.configuration();
    while (!files.empty()) {
        const std::string file = files.front();
        files.erase(files.begin());
        JPPlacementsHolder* holder = nullptr;
        for (const auto& b : config.boards())
            if (b->file == file && b->dirty) holder = b.get();
        for (const auto& p : config.panels())
            if (p->file == file && p->dirty) holder = p.get();
        if (holder) {
            confirmSave(*holder, [this, files, then] { confirmSaveAll(files, then); });
            return;
        }
    }
    if (then) then();
}

void JPlacerOpenPnpTabs::saveConfiguration(std::function<void()> then) {
    m_job.configurationChanged();
    std::vector<std::string> files;
    for (const auto& b : m_job.configuration().boards()) files.push_back(b->file);
    for (const auto& p : m_job.configuration().panels()) files.push_back(p->file);
    confirmSaveAll(files, std::move(then));
}

bool JPlacerOpenPnpTabs::mayClose() {
    if (m_closing) return true;
    bool dirty = false;
    for (const auto& b : m_job.configuration().boards()) dirty = dirty || b->dirty;
    for (const auto& p : m_job.configuration().panels()) dirty = dirty || p->dirty;
    if (!dirty) return true;
    saveConfiguration([this] {
        m_closing = true;
        m_window.requestClose();
    });
    return false;
}

bool JPlacerOpenPnpTabs::showDock(const std::string& title) {
    for (JDockWidget* d : { m_jobDock.get(), m_panelsDock.get(), m_boardsDock.get(), m_partsDock.get(), m_packagesDock.get(),
                             m_visionDock.get(), m_feedersDock.get() })
        if (title == d->title()) {
            m_layout.show(d);
            return true;
        }
    return false;
}

} // inline namespace jf

inline namespace jf {

void JPlacerOpenPnpTabs::ensurePhotonActuator() {
    for (const JPFeeder& f : m_job.configuration().feeders())
        if (f.isPhoton()) {
            m_machine.ensurePhotonActuator();
            return;
        }
}

} // inline namespace jf
