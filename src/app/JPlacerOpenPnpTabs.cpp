// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerOpenPnpTabs.h"
#include "JPlacerJobMachine.h"

#include "model/JPLengthUnits.h"

#include "JPlacerBlindsFiles.h"
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

#include "model/JPBlindsFeeders.h"
#include "model/JPDefinitionChanges.h"
#include "tasks/JPRotationMode.h"
#include "tasks/JPFeederActions.h"
#include "tasks/JPFeederFeed.h"
#include "tasks/JPFeederPipelines.h"
#include "tasks/JPFeederTakeBack.h"
#include "tasks/JPVisionPipelinePrep.h"

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
// The camera within this of a place is there (mm).
constexpr double kAtLocationMm = 0.001;
}

JPlacerOpenPnpTabs::JPlacerOpenPnpTabs(JAppWindow& window, JSceneGraph& graph, JPlacerJob& job, JPlacerMachine& machine)
    : m_window(window), m_job(job), m_machine(machine), m_layout(machine.layout()), m_pipelines(window, machine) {
    m_machine.setConfiguration(&job.configuration());
    // As OpenPnP's vision tape and blinds feeders: unhomed, their calibration is no longer true.
    m_machine.onUnhomed = [this] {
        for (JPFeeder& f : m_job.configuration().feeders()) {
            if (f.isVisionTape()) f.visionOffset.reset();
            // A blinds feeder's fiducials and cover no longer known.
            if (f.typeName() == "BlindsFeeder") {
                f.blinds.calibrated = false;
                f.blinds.coverPositionMm.reset();
            }
        }
    };
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
    // As OpenPnP's PackageCompositingWizard: the camera looking up, the first nozzle tip that can take the package.
    m_packages->computeComposite = [this](const JPPackage& pkg, JPPackagesPanel::CompositePreview& out, std::string& why) {
        const JPCell* cell = m_machine.cell();
        if (!cell) {
            why = "no machine is open";
            return false;
        }
        // The camera bottom vision uses (JPlacerMachine::upCameraFeed), as the machine has it now.
        const JPCameraConfig* camera = nullptr;
        if (const JPCameraFeed* up = m_machine.upCameraFeed())
            for (const JPCameraConfig& c : cell->config().cameras)
                if (c.id == up->config().id) camera = &c;
        if (!camera) {
            why = "no camera looks up";
            return false;
        }
        const JPNozzleTipConfig* tip = nullptr;
        for (const JPNozzleTipConfig& t : cell->config().nozzleTips)
            if (!tip && std::find(pkg.compatibleNozzleTipIds.begin(), pkg.compatibleNozzleTipIds.end(), t.id) != pkg.compatibleNozzleTipIds.end())
                tip = &t;
        if (!tip) {
            why = "No compatible nozzle tip found for " + pkg.id + ".";
            return false;
        }
        // What the camera sees: by its calibration, else its pixel size and picture size.
        const auto calibrations = cell->cameraCalibrations(camera->id);
        if (!calibrations.empty() && calibrations.front().scaleX() > 0 && calibrations.front().scaleY() > 0) {
            const JPCameraCalibration& cal = calibrations.front();
            out.cameraWidthMm = cal.width / cal.scaleX();
            out.cameraHeightMm = cal.height / cal.scaleY();
        } else {
            out.cameraWidthMm = camera->device["width"].number(0.0) * camera->unitsPerPixelX;
            out.cameraHeightMm = camera->device["height"].number(0.0) * camera->unitsPerPixelY;
        }
        if (out.cameraWidthMm <= 0 || out.cameraHeightMm <= 0) {
            why = camera->name + " is not calibrated: what it sees is not known";
            return false;
        }
        const JPVisionSettings* settings = m_job.configuration().visionSettings(pkg.bottomVisionId);
        if (!settings) settings = m_job.configuration().visionSettings(machineVisionDefaults().first);
        if (!settings) {
            why = "no bottom vision settings for " + pkg.id;
            return false;
        }
        auto composite = JPVisionPipelinePrep::composite(pkg, *settings, camera, out.cameraWidthMm, out.cameraHeightMm, tip);
        out.composite = composite;
        out.roamingRadiusMm = camera->roamingRadiusMm;
        out.footprintMm = pkg.footprint.inMillimeters();
        return true;
    };
    m_packagesDock = std::make_unique<JDockWidget>("Packages", 0.f, 0.f, 0.f, 0.f);
    m_packagesDock->setContent(m_packages.get());
    m_layout.add(m_packagesDock.get(), JPlacerLayout::Home::Work);

    // Vision: the vision settings; the machine's defaults from the cell.
    m_vision = std::make_unique<JPVisionSettingsPanel>(graph, job.configuration(),
                                                       JSettings::instance().get<double>(JPlacerSettings::kVisionSplit, kSplit));
    m_vision->onChanged = [this] {
        m_job.configurationChanged();
        m_machine.refreshSetupForm();   // Machine Setup's vision nodes show the defaults too
    };
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
    m_feeders->moveTo = [this](JPFeedersPanel::Tool t, const JPFeedersPanel::Where& to, bool straight) {
        m_machine.moveToolTo(t, to, straight);
    };
    m_feeders->whereIsActuator = [this](const std::string& name) { return m_machine.whereIsActuator(name); };
    m_feeders->moveActuatorTo = [this](const std::string& name, const JPFeedersPanel::Where& to, bool straight) {
        m_machine.moveActuatorTo(name, to, straight);
    };
    m_feeders->cameraView = [this] { return m_machine.headCameraView(); };
    m_feeders->setHal(&m_window.hal());
    m_feeders->machineFeed = [this](const std::string& feederId, bool pick) {
        const std::string nozzle = m_machine.chosenNozzleId();
        m_jobRun->machineTask([this, feederId, pick, nozzle](JPJobMachine& machine,
                                                              const std::function<void(const std::function<void()>&)>& onMain,
                                                              std::string& why) {
            // OpenPnP's pickFeeder: the nozzle's tip must fit the part's package; with Change On Manual Pick, a
            // fitting tip no nozzle has is put on it; else what to do is said.
            if (pick && !nozzle.empty()) {
                std::vector<std::string> fit;
                std::string packageId;
                onMain([&] {
                    if (const JPFeeder* f = m_job.configuration().feeder(feederId))
                        if (const JPPart* p = m_job.configuration().part(f->partId()))
                            if (const JPPackage* k = m_job.configuration().package(p->packageId)) {
                                fit = k->compatibleNozzleTipIds;
                                packageId = k->id;
                            }
                });
                const auto nozzles = machine.nozzles();
                const auto self = std::find_if(nozzles.begin(), nozzles.end(), [&](const JPJobMachine::Nozzle& n) { return n.id == nozzle; });
                auto tipName = [&machine](const std::string& id) {
                    for (const auto& [tid, name] : machine.tips()) if (tid == id) return name;
                    return id;
                };
                if (self != nozzles.end() && !packageId.empty()
                    && (self->tipId.empty() || std::find(fit.begin(), fit.end(), self->tipId) == fit.end())) {
                    const JPJobMachine::Nozzle* alt = nullptr;
                    bool changed = false;
                    for (const std::string& t : fit) {
                        const auto on = std::find_if(nozzles.begin(), nozzles.end(), [&](const JPJobMachine::Nozzle& n) { return n.tipId == t; });
                        if (on == nozzles.end() && std::find(self->tipIds.begin(), self->tipIds.end(), t) != self->tipIds.end()
                            && self->tipChangeOnManualPick) {
                            if (!machine.changeTip(nozzle, t, why)) return false;
                            changed = true;
                            break;
                        }
                        if (!alt && on != nozzles.end()) alt = &*on;
                    }
                    if (!changed) {
                        why = self->tipId.empty() ? "No nozzle tip loaded on nozzle " + self->name + ". "
                                                  : "Nozzle " + self->name + " loaded nozzle tip " + tipName(self->tipId)
                                                        + " is not compatible with package " + packageId + ". ";
                        if (alt) why += "Consider selecting nozzle " + alt->name + ", it has compatible nozzle tip " + tipName(alt->tipId) + " loaded. ";
                        else if (!self->tipChangeOnManualPick)
                            why += "You may want to enable automatic nozzle tip change on manual pick on the Nozzle / Tool Changer. ";
                        why += "The pick will always be performed with the nozzle selected in the Machine Controls. ";
                        return false;
                    }
                }
            }
            bool empty = false;
            if (!JPFeederFeed::feed(m_job.configuration(), feederId, nozzle, machine, onMain, why, empty)) return false;
            if (!pick) return true;
            std::optional<JPLocation> at;
            std::string kind;
            onMain([&] {
                if (const JPFeeder* f = m_job.configuration().feeder(feederId)) {
                    at = f->pickLocation();
                    kind = f->typeName();
                    // Picked from on top of the part, as high as it is.
                    if (const JPPart* p = m_job.configuration().part(f->partId()); at && p && f->partHeightAbovePickLocation())
                        at = at->add(JPLocation(p->height.units(), 0, 0, p->height.value(), 0));
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
            std::string part;
            onMain([&] {
                if (const JPFeeder* f = m_job.configuration().feeder(feederId)) part = f->partId();
            });
            if (!machine.safeZ(why)) return false;
            // OpenPnP's pickFeeder: the part-off check first, at safe Z.
            if (machine.vacuumChecked(nozzle, JPJobMachine::VacuumStep::BeforePick)) {
                bool off = false;
                if (!machine.partOff(nozzle, off, why)) return false;
                if (!off) {
                    why = "Part vacuum-detected on nozzle before pick.";
                    return false;
                }
            }
            // As OpenPnP's: the nozzle made able to turn the part from its pick to a
            // test placement (bottom vision's Test Alignment Angle).
            double testAngle = 0;
            onMain([&] {
                if (const JPCell* c = m_machine.cell()) testAngle = c->config().vision.testAlignmentAngle;
            });
            JPRotationMode::prepare(machine, nozzle, at->rotation(), testAngle);
            // The nozzle given the part first, as OpenPnP's pick(part): its levels.
            machine.holding(nozzle, part);
            if (!machine.pick(nozzle, *at, why)) {
                machine.holding(nozzle, "");
                return false;
            }
            if (!JPFeederFeed::postPick(m_job.configuration(), feederId, machine, onMain, why) || !machine.safeZ(why)) return false;
            // Then the part-on check.
            if (machine.vacuumChecked(nozzle, JPJobMachine::VacuumStep::AfterPick)) {
                bool on = false;
                if (!machine.partOn(nozzle, on, why)) return false;
                if (!on) {
                    why = "No part detected.";
                    return false;
                }
            }
            return true;
        });
    };
    // OpenPnP's Cycles.zProbe: the head's Z probe over the place at safe Z, read (mm from where it is), and back.
    auto probeZ = [this](double x, double y, std::function<void(double)> done) {
        std::string probe;
        if (const JPCell* c = m_machine.cell())
            for (const JPHeadConfig& h : c->config().heads)
                if (!h.zProbeActuatorId.empty())
                    for (const JPActuatorConfig& a : c->config().actuators)
                        if (a.id == h.zProbeActuatorId) probe = a.name.empty() ? a.id : a.name;
        if (probe.empty()) return false;
        return m_jobRun->machineTask([this, probe, x, y, done](JPJobMachine& machine,
                                                             const std::function<void(const std::function<void()>&)>& onMain,
                                                             std::string& why) {
            JPlacerMachine::Where was;
            onMain([&] { was = m_machine.whereIsActuator(probe); });
            if (!machine.safeZ(why)) return false;
            if (!machine.moveActuator(probe, JPLocation(JPLengthUnit::Millimeters, x, y, 0, 0), false, 1.0, why)) return false;
            std::string reading;
            if (!machine.readActuator(probe, "", reading, why)) return false;
            char* end = nullptr;
            const double z = std::strtod(reading.c_str(), &end);
            if (end == reading.c_str()) {
                why = "Z Probe " + probe + " conversion failed (" + reading + ")";
                return false;
            }
            // Back where it was.
            if (was[0] && was[1] && !machine.moveActuator(probe, JPLocation(JPLengthUnit::Millimeters, *was[0], *was[1], 0, 0),
                                                          false, 1.0, why))
                return false;
            const double at = was[2].value_or(0) + z;
            onMain([done, at] { done(at); });
            return true;
        });
    };
    m_feeders->probeZ = probeZ;
    m_machine.probeZ = probeZ;
    m_feeders->actuatorNames = [this] {
        std::vector<std::string> out;
        if (const JPCell* c = m_machine.cell())
            for (const JPActuatorConfig& a : c->config().actuators) out.push_back(a.name.empty() ? a.id : a.name);
        return out;
    };
    // The Jog panel's Recycle (OpenPnP's recycleAction): as a machine task.
    m_machine.canRecycle = [this](const std::string& partId) {
        std::optional<JPLocation> camera;
        return !JPFeederTakeBack::feederFor(m_job.configuration(), partId, camera).empty();
    };
    m_machine.recycle = [this](const std::string& nozzleId) {
        m_jobRun->machineTask([this, nozzleId](JPJobMachine& machine, const std::function<void(const std::function<void()>&)>& onMain,
                                               std::string& why) {
            const bool ok = JPFeederTakeBack::takeBack(m_job.configuration(), nozzleId, machine, onMain,
                                                       &m_machine.scripting(), why);
            onMain([&] {
                m_feeders->refresh();
                m_job.configurationChanged();
                m_machine.refreshRecycle();
            });
            return ok;
        });
    };
    m_feeders->machineAction = [this](const std::string& feederId, const std::string& action) {
        if (action.rfind("photon", 0) == 0) ensurePhotonActuator();
        const bool started = m_jobRun->machineTask([this, feederId, action](JPJobMachine& machine,
                                                       const std::function<void(const std::function<void()>&)>& onMain,
                                                       std::string& why) {
            JPFeederActions::Outcome outcome;
            JPVisionConfig vision;
            onMain([&] {
                if (const JPCell* c = m_machine.cell()) vision = c->config().vision;
            });
            const bool ok = JPFeederActions::run(m_job.configuration(), feederId, action, machine, onMain, vision,
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
                if (ok && !outcome.report.empty()) JDialog::message("OCR Report", outcome.report);
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
    // A feeder's pipeline (an advanced loose part feeder's training one too):
    // edited on the head camera, or its default back.
    m_feeders->pipelineAction = [this](const std::string& feederId, const std::string& action) {
        JPFeeder* f = m_job.configuration().feeder(feederId);
        if (!f) return;
        const bool training = action.find("Training") != std::string::npos;
        // A heap feeder's detection pipeline is its feeder pipeline; its drop box's, the box's.
        const bool heap = f->typeName() == "ReferenceHeapFeeder";
        const std::string element = training ? "training-pipeline" : heap ? "feeder-pipeline" : "pipeline";
        JPDropBoxes& boxes = m_job.configuration().dropBoxes();
        if (action.find("DropBox") != std::string::npos) {
            const std::string box = JPFeederPipelines::dropBoxOf(*f, boxes);
            if (action.rfind("reset", 0) == 0) {
                if (JPFeederPipelines::resetDropBox(boxes, box)) m_job.configurationChanged();
                return;
            }
            std::optional<JPPipeline> held = JPFeederPipelines::ofDropBox(boxes, box);
            if (!held) return;
            auto pipeline = std::make_shared<JPPipeline>(std::move(*held));
            m_pipelines.useHeadCamera(*pipeline, m_job.configuration().directory());
            m_pipelines.edit(box + " Part-Pipeline", pipeline, [this, box](const JPPipeline& edited) {
                m_job.configuration().dropBoxes().setPartPipeline(box, edited.toXml());
                m_job.configurationChanged();
            });
            return;
        }
        // A blinds feeder's, the same for every feeder on its holder.
        if (f->typeName() == "BlindsFeeder" && action.rfind("reset", 0) == 0) {
            if (JPFeederPipelines::reset(*f)) {
                JPBlindsFeeders::propagate(m_job.configuration(), feederId);
                m_job.configurationChanged();
            }
            return;
        }
        if (heap && action.rfind("reset", 0) == 0) {
            if (JPFeederPipelines::reset(*f, element, &boxes)) m_job.configurationChanged();
            return;
        }
        if (action.rfind("reset", 0) == 0) {
            // A Bamboo feeder's to its Vision Type's default, as OpenPnP's asks.
            if (f->isVisionTape()) {
                JDialog::confirm("Warning",
                                 "This will reset the pipeline to the " + f->text("pipeline-type", "CircularSymmetry")
                                     + " type default. Are you sure?",
                                 [this, feederId] {
                                     if (JPFeeder* kept = m_job.configuration().feeder(feederId); kept && JPFeederPipelines::reset(*kept))
                                         m_job.configurationChanged();
                                 });
                return;
            }
            if (JPFeederPipelines::reset(*f, element)) m_job.configurationChanged();
            return;
        }
        // A Bamboo feeder's camera over its holes first, when wanted.
        if (f->isVisionTape()) {
            editTapePipeline(feederId);
            return;
        }
        editFeederPipeline(feederId, element);
    };
    m_feeders->extractBlindsFiles = [] { JPlacerBlindsFiles::extract(); };
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
        context.renderingSmooth = [this](const std::string& id) { return m_machine.cameraRenderingSmooth(id); };
        context.cameraControls = [this](const std::string& id) { return m_machine.cameraDeviceControls(id); };
        context.calibrateCamera = [this](const std::string& id, std::function<void(bool)> finished) {
            m_machine.calibrateCamera(id, std::move(finished));
        };
        context.calibrateBacklash = [this](const std::string& id, std::function<void(bool)> finished) {
            m_machine.calibrateBacklash(id, std::move(finished));
        };
        context.calibrateTip = [this](const std::string& id, std::function<void(bool)> finished) {
            m_machine.calibrateTip(id, std::move(finished));
        };
        context.enableVisualHoming = [this](const std::string& id, std::function<void(bool)> finished) {
            m_machine.enableVisualHoming(id, std::move(finished));
        };
        context.tablesLinked = [] { return JSettings::instance().get<bool>(JPlacerSettings::kTablesLinked, false); };
        context.setTablesLinked = [](bool linked) {
            JSettings::instance().set(JPlacerSettings::kTablesLinked, linked);
            JPlacerSettings::save();
        };
        context.setRenderingSmooth = [this](const std::string& id, bool smooth) { m_machine.setCameraRenderingSmooth(id, smooth); };
        context.homed = [this] { return m_machine.cell() && m_machine.cell()->isHomed(); };
        context.home = [this] { m_machine.home(); };
        context.axisPosition = [this](const std::string& axisId) -> std::optional<double> {
            const JPCell* c = m_machine.cell();
            if (!c || !c->isConnected()) return std::nullopt;
            const auto p = c->positions();
            const auto i = p.find(axisId);
            return i == p.end() ? std::nullopt : std::optional(i->second);
        };
        context.configurationChanged = [this] { m_job.configurationChanged(); };
        context.firmwareProfile = [this](const std::string& driverId) {
            const JPCell* c = m_machine.cell();
            if (!c) return std::string();
            const auto all = c->firmware();
            const auto it = all.find(driverId);
            return it == all.end() ? std::string() : it->second;
        };
        context.firmwareIdentity = [this](const std::string& driverId) {
            const JPCell* c = m_machine.cell();
            if (!c) return std::string();
            const auto all = c->firmwareIdentity();
            const auto it = all.find(driverId);
            return it == all.end() ? std::string() : it->second;
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
    // What scripts ask of the job (OpenPnP's gui.jobTab, Utils2D, VisionUtils.readQrCode).
    m_machine.scriptJobMachine = [this]() -> JPlacerJobMachine* { return &m_jobRun->jobMachine(); };
    m_machine.onScriptJobRequest = [this](const JJson& request) { return scriptJobRequest(request); };
    m_machine.jobBoards = [this] {
        std::vector<const JPBoardLocation*> boards;
        for (const JPBoardLocation* b : m_job.job().boardLocations()) boards.push_back(b);
        return boards;
    };
    // The vision pages' tests, on the machine as the job runs it.
    m_visionTests = std::make_unique<JPlacerVisionTests>(m_job, m_machine, *m_jobRun);
    auto visionTest = [this](const std::string& id, const JPVisionForms::Holder& holder, const std::string& test) {
        m_visionTests->run(id, holder, test, [this] { m_job.configurationChanged(); });
    };
    m_parts->visionTest = visionTest;
    m_packages->visionTest = visionTest;
    m_vision->visionTest = visionTest;
    m_parts->setTests(m_visionTests->tests());
    // Machine Setup's vision nodes: their default settings' page, as the Vision tab's.
    m_machine.setSetupVisionTests(m_visionTests->tests());
    m_machine.onSetupVisionAction = [this](const std::string& id, const std::string& action) { m_vision->act(id, action); };
    m_machine.onSetupConfigurationChanged = [this] {
        m_vision->refresh();
        m_job.configurationChanged();
    };
    m_machine.setBoardsZ = [this](double z) {
        for (JPBoardLocation* b : m_job.job().boardLocations()) {
            const JPLocation l = b->globalLocation();
            b->setGlobalLocation(l.derive(std::nullopt, std::nullopt, z / JPLengthUnits::toMillimeters(l.units()), std::nullopt));
        }
        m_job.changed();
    };
    m_packages->setTests(m_visionTests->tests());
    m_vision->setTests(m_visionTests->tests());
    // A strip feeder's Auto Setup, on the head camera.
    m_autoSetup = std::make_unique<JPlacerStripAutoSetup>(m_job, m_machine, *m_jobRun, m_pipelines);
    m_autoSetup->onStateChanged = [this] { m_feeders->rebuild(); };
    m_autoSetup->onFeederChanged = [this] { m_job.configurationChanged(); };
    m_feeders->autoSetupRunning = [this] { return m_autoSetup->running(); };
    // A push-pull feeder's Setup OCR Region.
    m_ocrRegion = std::make_unique<JPlacerOcrRegionSetup>(m_job, m_machine, *m_jobRun);
    m_ocrRegion->onStateChanged = [this] { m_feeders->rebuild(); };
    m_ocrRegion->onFeederChanged = [this] { m_job.configurationChanged(); };
    m_feeders->ocrRegionStep = [this] { return m_ocrRegion->running() ? m_ocrRegion->proceedLabel() : std::string(); };
    m_feeders->ocrRegion = [this](const std::string& feederId, const std::string& action) {
        if (action == "ocrRegionCancel") m_ocrRegion->cancel();
        else if (action == "ocrRegionNext") m_ocrRegion->next();
        else m_ocrRegion->start(feederId);
    };
    m_feeders->autoSetup = [this](const std::string& feederId, const std::string& action) {
        if (action == "autoSetupCancel") m_autoSetup->cancel();
        else m_autoSetup->start(feederId);
    };
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

    m_jobPanel->defaultLocation = [this] {
        JPMachineLocation l;
        if (const JPCell* c = m_machine.cell()) l = c->config().defaultBoardLocation;
        return JPLocation(JPLengthUnit::Millimeters, l.x, l.y, l.z, l.rotation);
    };
    m_links = std::make_unique<JPlacerTableLinks>(
        JPlacerTableLinks::Tabs { *m_jobPanel, *m_boards, *m_panels, *m_parts, *m_packages, *m_feeders, *m_vision,
                                  *m_jobDock, *m_boardsDock, *m_panelsDock, *m_partsDock, *m_feedersDock },
        m_job.configuration(), [] { return JSettings::instance().get<bool>(JPlacerSettings::kTablesLinked, false); });

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
    m_autoSetup.reset();   // its look at the camera stopped first
    m_ocrRegion.reset();
    m_machine.setConfiguration(nullptr);
    m_machine.onUnhomed = nullptr;
    m_machine.onSetupVisionAction = nullptr;
    m_machine.onSetupConfigurationChanged = nullptr;
    m_machine.setBoardsZ = nullptr;
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
    m_machine.onScriptJobRequest = nullptr;
    m_machine.jobBoards = nullptr;
    m_machine.scriptJobMachine = nullptr;
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

void JPlacerOpenPnpTabs::editFeederPipeline(const std::string& feederId, const std::string& element) {
    JPFeeder* f = m_job.configuration().feeder(feederId);
    if (!f) return;
    const bool training = element == "training-pipeline";
    // A loose part or heap feeder's is titled by its part, which it must have.
    const std::string kind = f->typeName();
    const bool heap = kind == "ReferenceHeapFeeder";
    const bool loose = kind == "ReferenceLoosePartFeeder" || kind == "AdvancedLoosePartFeeder" || heap;
    if (loose && !m_job.configuration().part(f->partId())) {
        JDialog::message("Error", "Feeder " + f->name() + " has no part.");
        return;
    }
    std::optional<JPPipeline> held = JPFeederPipelines::of(*f, element, &m_job.configuration().dropBoxes());
    if (!held) return;
    auto pipeline = std::make_shared<JPPipeline>(std::move(*held));
    m_pipelines.useHeadCamera(*pipeline, m_job.configuration().directory());
    JPFeederPipelines::configureForEditing(m_job.configuration(), *f, *pipeline);
    // A blinds feeder's OCR as it reads, about where the camera is.
    if (kind == "BlindsFeeder")
        if (const auto at = m_machine.toolLocation(JPSetupForm::Tool::Camera))
            JPFeederPipelines::setupBlindsOcr(m_job.configuration(), *f, *pipeline, *at, f->text("ocr-action", "None"));
    const std::string title = (loose ? f->partId() : f->name())
                              + (heap ? (training ? " Training-Pipeline" : " Feeder-Pipeline") : training ? " Training Pipeline" : " Pipeline");
    m_pipelines.edit(title, pipeline, [this, feederId, element](const JPPipeline& edited) {
        if (JPFeeder* kept = m_job.configuration().feeder(feederId)) {
            kept->setPipeline(edited.toXml(), element);
            if (kept->typeName() == "BlindsFeeder") JPBlindsFeeders::propagate(m_job.configuration(), feederId);
            m_job.configurationChanged();
        }
    });
}

void JPlacerOpenPnpTabs::editTapePipeline(const std::string& feederId) {
    JPFeeder* f = m_job.configuration().feeder(feederId);
    if (!f) return;
    // Its vision location: the middle of its holes (none set: where the camera is).
    const JPLocation h1 = f->locationOf("hole-1-location"), h2 = f->locationOf("hole-2-location");
    const std::optional<JPLocation> camera = m_machine.toolLocation(JPSetupForm::Tool::Camera);
    if (!h1.isInitialized() || !h2.isInitialized() || !camera) {
        editFeederPipeline(feederId, "pipeline");
        return;
    }
    const JPLocation mid = h1.add(h2).multiply(0.5).convertToUnits(JPLengthUnit::Millimeters);
    if (std::abs(mid.x() - camera->x()) < kAtLocationMm && std::abs(mid.y() - camera->y()) < kAtLocationMm) {
        editFeederPipeline(feederId, "pipeline");
        return;
    }
    const std::string move = "move the camera to the proper feeder vision location before editing the pipeline";
    if (!m_machine.cell() || !m_machine.cell()->isConnected()) {
        JDialog::confirm("Warning", "Machine not enabled, unable to " + move + ".\nDo you want to proceed anyway?",
                         [this, feederId] { editFeederPipeline(feederId, "pipeline"); });
        return;
    }
    m_window.openModal<JPlacerChoiceDialog>(
        "Select an Option", "Do you want to " + move + "?", std::vector<std::string> { "Yes", "No", "Cancel" }, 2,
        [this, feederId, mid, alive = std::weak_ptr<bool>(m_alive)](int chosen) {
            if (chosen == 1) editFeederPipeline(feederId, "pipeline");
            if (chosen != 0) return;
            m_jobRun->machineTask([this, feederId, mid, alive](JPJobMachine& machine,
                                                               const std::function<void(const std::function<void()>&)>& onMain,
                                                               std::string& why) {
                if (!machine.positionCamera(mid, why)) return false;
                onMain([&] {
                    if (const auto a = alive.lock(); a && *a) editFeederPipeline(feederId, "pipeline");
                });
                return true;
            });
        });
}

void JPlacerOpenPnpTabs::ensurePhotonActuator() {
    for (const JPFeeder& f : m_job.configuration().feeders())
        if (f.isPhoton()) {
            m_machine.ensurePhotonActuator();
            return;
        }
}

JJson JPlacerOpenPnpTabs::scriptJobRequest(const JJson& request) {
    JJson answer = JJson::object();
    const std::string call = request["call"].str();
    const std::vector<JPBoardLocation*> boards = m_job.job().boardLocations();
    const JJson& index = request["board"];
    JPBoardLocation* board = index.isNumber() && index.number() >= 0 && size_t(index.number()) < boards.size()
                           ? boards[size_t(index.number())] : nullptr;
    auto mm = [](const JPLocation& l) {
        const JPLocation m = l.convertToUnits(JPLengthUnit::Millimeters);
        JJson o = JJson::object();
        o["x"] = m.x();
        o["y"] = m.y();
        o["z"] = m.z();
        o["rotation"] = m.rotation();
        return o;
    };
    if (call == "job") {
        JJson list = JJson::array();
        for (const JPBoardLocation* b : boards) {
            JJson o = JJson::object();
            o["id"] = b->id;
            o["name"] = std::filesystem::path(b->fileName).filename().string();
            o["side"] = b->globalSide() == JPSide::Bottom ? "Bottom" : "Top";
            o["enabled"] = b->locallyEnabled;
            o["location"] = mm(b->globalLocation());
            list.push(o);
        }
        answer["result"]["file"] = m_job.job().file;
        answer["result"]["boards"] = list;
    } else if (!board && call != "refreshJob") {
        answer["error"] = std::string("no such board in the job");
    } else if (call == "setBoardEnabled") {
        board->locallyEnabled = request["enabled"].boolean();
        m_job.changed();
    } else if (call == "boardPlacementLocation") {
        const JJson& l = request["location"];
        answer["result"] = mm(board->placementLocation(JPLocation(JPLengthUnit::Millimeters, l["x"].number(), l["y"].number(),
                                                                  l["z"].number(), l["rotation"].number())));
    } else if (call == "refreshJob") {
        m_job.changed();
    }
    return answer;
}

} // inline namespace jf
