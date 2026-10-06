// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerMachine.h"
#include "JPlacerJobMachine.h"
#include "JPlacerSlotVision.h"
#include "model/JPBoardLocation.h"
#include "model/JPBoard.h"
#include "camera/JPImageFile.h"

#include <opencv2/imgproc.hpp>

#include "JPlacerClassSelectionDialog.h"
#include "JPlacerSettings.h"

#include "camera/JPSimulatedViewpoint.h"
#include "camera/JPWhiteBalance.h"
#include "common/JPlacerLog.h"
#include "common/JPlacerPaths.h"
#include "model/JPLengthUnits.h"
#include "model/JPXmlValues.h"
#include "openpnp/JPOpenPnpMachineImporter.h"
#include "setup/JPSetupEdits.h"
#include "tasks/JPPhotonBus.h"
#include "ui/JPActuatorPanel.h"
#include "ui/JPConsolePanel.h"
#include "ui/JPIcons.h"
#include "ui/JPJogPanel.h"
#include "ui/JPMachinePanel.h"
#include "ui/JPMachineSetupPanel.h"
#include "ui/JPUiParts.h"

#include <j/config/Settings.h>
#include <j/core/Dialog.h>
#include <j/core/JStyle.h>
#include <j/core/Log.h>
#include <j/core/MainThreadDispatcher.h>

#include <cstdlib>
#include <filesystem>
#include <future>
#include <map>
#include <thread>

inline namespace jf {

namespace {

// The file an imported OpenPnP machine is written to, in cellsDir().
constexpr const char* kImportedCellFile = "openpnp.json";
// How long a status-bar message stays: a confirmation briefly, a failure long
// enough to read the reason.
constexpr int kStatusMs = 3000;
constexpr int kErrorMs  = 8000;
// How long the background calibration's problem pictures are shown.
constexpr int kProblemsMs = 10000;
// A script's Home: how long it is waited for, looked at this often (ms).
constexpr int kScriptHomeMs = 120000;
constexpr int kScriptPollMs = 50;
// A simulated nozzle tip seen from below: this wide when its tip gives no diameter (mm), and this bright.
constexpr double kSimulatedTipMm = 1.0;
constexpr float  kSimulatedTipLevel = 230;
// A part on a tip as OpenPnP's SimulatedUpCamera draws it: its body dark grey (60), its pads white.
constexpr float  kSimulatedBodyLevel = 60;
constexpr float  kSimulatedPadLevel = 255;
// How near the camera's centre a nozzle must be for Adjust Camera Z (OpenPnP's 0.1 mm).
constexpr double kCenteredMm = 0.1;
// OpenPnP's Mapped Roughly and Mapped Finely white balance: the brightness levels mapped.
constexpr int kMappedRoughlyLevels = 8, kMappedFinelyLevels = 32;

// How fast Park Head moves, as a share of the axes' rates.

// Where OpenPnP keeps its machine, under the home folder.
constexpr const char* kOpenPnpDir         = ".openpnp2";
constexpr const char* kOpenPnpMachineFile = "machine.xml";
// OpenPnP's samples, shipped (openpnp-defaults) and copied for a first start.
constexpr const char* kSamplesDir = "samples";
// The scripts' folder, in the configuration and among OpenPnP's defaults (its Example scripts).
constexpr const char* kScriptsDir = "scripts";
// A camera brought forward for a task runs this long at least, shown or not (each look renews it).
constexpr int kTaskCameraMs = 10000;

} // namespace

JPlacerMachine::JPlacerMachine(JAppWindow& window, JSceneGraph& graph)
    : m_window(window), m_graph(graph), m_layout(window), m_profiles(JPFirmwareProfile::loadAll()),
      m_connectIcon(graph), m_homeIcon(graph),
      m_position(graph, [this] { return m_jog ? m_jog->where() : std::vector<std::pair<std::string, double>>(); }) {
    m_scripting = std::make_shared<JPScripting>((std::filesystem::path(JPlacerPaths::configDir()) / kScriptsDir).string());
    // As OpenPnP: its Example scripts put beside the scripts, each that is not there yet.
    if (const std::string shipped = JPlacerPaths::bundled(JPOpenPnpMachineImporter::kDefaultsDir); !shipped.empty()) {
        const std::filesystem::path from = std::filesystem::path(shipped) / kScriptsDir;
        const std::filesystem::path to = std::filesystem::path(JPlacerPaths::configDir()) / kScriptsDir;
        std::error_code ec;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(from, ec)) {
            if (!entry.is_regular_file()) continue;
            const std::filesystem::path target = to / std::filesystem::relative(entry.path(), from, ec);
            std::filesystem::create_directories(target.parent_path(), ec);
            std::filesystem::copy_file(entry.path(), target, std::filesystem::copy_options::skip_existing, ec);
        }
    }
    m_scripting->api = [this, alive = std::weak_ptr<bool>(m_alive)](const JJson& request) {
        JJson answer = JJson::object();
        if (const auto a = alive.lock(); !a || !*a) answer["error"] = std::string("jplacer is closing");
        else answer = scriptRequest(request);
        return answer;
    };
    // The machine's two states, always in view: click the chip to connect or
    // disconnect, the house to home.
    JToolBar& tb = window.toolBar();
    tb.addWidget(&m_connectIcon);
    tb.addWidget(&m_homeIcon);
    m_connectIcon.onClicked.connect([this] {
        if (m_cell && m_cell->isConnected()) disconnect(); else connect();
    });
    m_homeIcon.onClicked.connect([this] { home(); });
    // Where the chosen tool is (Jog), at the right of the status bar.
    window.statusBar().addWidget(&m_position, JPPositionReadout::widthNeeded());
    JLOGC(JPlacerLog::kProfiles, JLogLevel::Info) << m_profiles.size() << " firmware profile(s)";
    if (const std::string last = JPlacerSettings::machineCell(); !last.empty()) {
        std::string error;
        if (!openCell(last, error)) JLOGC(JPlacerLog::kApp, JLogLevel::Warn) << error;
    }
}

JPlacerMachine::~JPlacerMachine() {
    *m_alive = false;
    dropPanels();         // they stop listening to the cell before the cell goes
    m_tipChanges.reset();
    m_testMotion.reset();
    m_cell.reset();
}

void JPlacerMachine::dropPanels(Keep keep) {
    const bool cameras = keep != Keep::SetupAndCameras;
    if (cameras) {
        m_cameraTasks.reset();   // a task under way finishes first: it drives the cell and a camera
    }
    for (const auto& u : m_unwatch) u();
    m_unwatch.clear();
    m_jog = nullptr;
    const JContainer* setup = keep == Keep::Nothing ? nullptr : m_setup;
    if (m_setup && !setup) {   // where its divider was, for the next one
        JSettings::instance().set(JPlacerSettings::kSetupTreeShare, m_setup->treeShare());
        JPlacerSettings::save();
        m_setup = nullptr;
    }
    for (Dock& d : m_docks) {
        if (d.panel.get() == setup) continue;
        d.dock->setContent(nullptr);
        d.panel.reset();
    }
    if (!cameras) return;
    for (CameraDock& c : m_cameras) m_layout.remove(c.dock.get());
    m_estimateZ.cancel();
    m_cameras.clear();
}

void JPlacerMachine::buildCameras() {
    // The cameras top left, each in a tab: what the machine sees is what the
    // person works from. A camera runs, and its light is on, while its tab is
    // in front (or torn out into a window of its own).
    // A head camera looks where its axes put it, plus its offset on the head;
    // a fixed one is drawn with nothing under it. The world it draws stays
    // where the switches put it when visual homing corrects the coordinates.
    const std::string captures = (std::filesystem::path(JPlacerPaths::configDir()) / "captures").string();
    std::vector<JPCameraPanel*> panels;
    // Where a tool on the head physically is: its axes, its offset, and the
    // correction visual homing made (the world stays where the switches put it).
    auto physical = [cell = m_cell.get()](const JPMountConfig& m, double& x, double& y) {
        const auto p = cell->positions();
        const auto px = p.find(m.axisX), py = p.find(m.axisY);
        if (px == p.end() || py == p.end()) return false;
        const auto corrected = cell->correctionSinceHome();
        const auto cx = corrected.find(m.axisX), cy = corrected.find(m.axisY);
        x = px->second + m.offsetX + (cx == corrected.end() ? 0 : cx->second);
        y = py->second + m.offsetY + (cy == corrected.end() ? 0 : cy->second);
        return true;
    };
    // OpenPnP's Simulation Mode, for the nozzle tips the up-looking cameras see.
    struct SimulatedTip {
        std::string   nozzleId;
        JPMountConfig mount;
        double        diameter;
    };
    std::vector<SimulatedTip> tips;
    for (const JPNozzleConfig& n : m_cell->config().nozzles) {
        double diameter = kSimulatedTipMm;
        for (const JPNozzleTipConfig& t : m_cell->config().nozzleTips)
            if (t.id == n.tipId && t.diameter > 0) diameter = t.diameter;
        tips.push_back({ n.id, n.mount, diameter });
    }
    for (const JPCameraConfig& c : m_cell->config().cameras) {
        std::function<bool(double&, double&)> view;
        if (!c.mount.axisX.empty() && !c.mount.axisY.empty())
            view = [cell = m_cell.get(), physical, m = c.mount, seen = std::make_shared<JPSimulatedViewpoint>()](double& x, double& y) {
                if (!physical(m, x, y)) return false;
                if (const JPSimulationConfig sim = cell->simulation(); sim.on()) seen->look(sim, JPSimulatedViewpoint::Clock::now(), x, y);
                return true;
            };
        else
            // A fixed camera looks from where it is, at the nozzle tips over it (simulated).
            view = [cell = m_cell.get(), at = c.mount](double& x, double& y) {
                if (!cell->simulation().on()) return false;
                x = at.offsetX;
                y = at.offsetY;
                return true;
            };
        CameraDock d;
        d.panel = std::make_unique<JPCameraPanel>(m_graph, m_window.hal(), c, captures, std::move(view),
                                                  [cell = m_cell.get()](const std::string& id, int width, int height) {
                                                      return cell->cameraCalibration(id, width, height);
                                                  });
        // OpenPnP's camera events, run where vision takes its pictures (off the screen's thread).
        // What OpenPnP's Simulation Mode adds to a simulated camera's picture:
        // the nozzle tips over a fixed one (going round on the runout), sparks
        // of noise, and dark while its light is off.
        // OpenPnP's own SimulatedUpCamera shows the nozzles over it whether or not
        // the machine is in Simulation Mode.
        const bool openPnpUp = c.device["openpnpClass"].str() == "SimulatedUpCamera";
        d.panel->feed().setExtras([cell = m_cell.get(), physical, tips, fixed = c.mount.headId.empty(), light = c.lightActuator(),
                                   openPnpUp, held = m_pnpChecking.holder()] {
            JPSimulatedSource::Extras e;
            const JPSimulationConfig sim = cell->simulation();
            if (!sim.on() && !openPnpUp) return e;
            if (sim.dynamic()) {
                e.sparks = sim.cameraNoise;
                e.dark = !light.empty() && !cell->switchedOn(light).value_or(false);
            }
            if (fixed) {
                const auto p = cell->positions();
                for (const SimulatedTip& t : tips) {
                    double x, y;
                    if (!physical(t.mount, x, y)) continue;
                    const auto r = p.find(t.mount.axisRotation);
                    const double axis = r == p.end() ? 0.0 : r->second;
                    if (sim.dynamic() && sim.runoutMm != 0) {
                        const double a = (axis - sim.runoutPhaseDeg) * M_PI / 180;
                        x += sim.runoutMm * std::cos(a);
                        y += sim.runoutMm * std::sin(a);
                    }
                    e.spots.push_back({ x, y, t.diameter, kSimulatedTipLevel });
                    // The part on it, as OpenPnP's SimulatedUpCamera draws it: its body
                    // dark grey, its pads white, turned as the part is (the nozzle's rotation).
                    const auto footprint = held(t.nozzleId);
                    if (!footprint) continue;
                    const double mm = JPLength(1, footprint->units).convertToUnits(JPLengthUnit::Millimeters).value();
                    const double a = (axis + cell->rotationModeOffset(t.nozzleId)) * M_PI / 180, ca = std::cos(a), sa = std::sin(a);
                    auto placed = [&](const JPFootprint::Outline& o, float level) {
                        JPSimulatedSource::Extras::Outline out { {}, level };
                        for (const JPFootprint::Point& pt : o)
                            out.points.push_back({ x + (pt.x * ca - pt.y * sa) * mm, y + (pt.x * sa + pt.y * ca) * mm });
                        e.outlines.push_back(std::move(out));
                    };
                    placed(footprint->bodyOutline(), kSimulatedBodyLevel);
                    for (const JPFootprint::Outline& o : footprint->padsOutlines()) placed(o, kSimulatedPadLevel);
                }
            }
            return e;
        });
        d.panel->feed().scriptEvent = [scripting = m_scripting, name = c.name](const std::string& event, std::string& why) {
            JJson g = JJson::object();
            g["camera"] = name;
            return scripting->on(event, g, why);
        };
        std::weak_ptr<bool> alive = m_alive;
        // OpenPnP's settling Diagnostics: each settle's graph and pictures shown on the camera's Camera Settling tab.
        d.panel->feed().onSettleTrace = [this, alive, id = c.id](const JPSettleTrace& trace) {
            JMainThreadDispatcher::instance().post([this, alive, id, trace] {
                if (const auto a = alive.lock(); !a || !*a || !m_setup) return;
                m_setup->measured([&](JPCellConfig& cell) {
                    for (JPCameraConfig& cam : cell.cameras)
                        if (cam.id == id) cam.settleTrace = trace;
                });
            });
        };
        // OpenPnP's SwitcherCamera: the device camera's feed (started if it is not
        // showing), and the actuator that switches the multiplexer to this camera.
        d.panel->feed().setSwitching({
            [this, alive, switched = c.id](const std::string& id) -> JPCameraFeed* {
                for (const CameraDock& other : m_cameras) {
                    if (other.panel->camera().id != id) continue;
                    JPCameraPanel* device = other.panel.get();
                    JMainThreadDispatcher::instance().post([alive, device, switched] {
                        if (const auto a = alive.lock(); a && *a) device->setFeeding(switched, true);
                    });
                    return &device->feed();
                }
                return nullptr;
            },
            [cell = m_cell.get()](const std::string& actuatorId, double value, std::string& why) {
                for (const JPActuatorConfig& a : cell->config().actuators)
                    if (a.id == actuatorId) {
                        if (a.valueType == JPActuatorConfig::ValueType::Boolean)
                            return cell->switchActuatorAndWait(actuatorId, value != 0, why);
                        return cell->setActuatorAndWait(actuatorId, JPXmlValues::number(value), why);
                    }
                why = "no actuator " + actuatorId;
                return false;
            } });
        // Straightened or as taken, kept from last time.
        d.panel->setView(JSettings::instance().get<bool>(JPlacerSettings::cameraStraightKey(c.id), false));
        d.panel->onViewChanged = [id = c.id](bool straight) {
            JSettings::instance().set(JPlacerSettings::cameraStraightKey(id), straight);
            JPlacerSettings::save();
        };
        d.panel->setReticle(JPReticle::fromText(
            JSettings::instance().get<std::string>(JPlacerSettings::cameraReticleKey(c.id), "")));
        d.panel->onReticleChanged = [id = c.id](const JPReticle& r) {
            JSettings::instance().set(JPlacerSettings::cameraReticleKey(id), r.toText());
            JPlacerSettings::save();
        };
        for (const auto& [key, overlay] : m_overlays) d.panel->setOverlay(key, overlay);
        // How much the wheel zooms, kept from last time.
        {
            using Z = JPCameraView::ZoomSensitivity;
            const std::string kept = JSettings::instance().get<std::string>(JPlacerSettings::cameraZoomKey(c.id), "");
            for (Z z : { Z::High, Z::Medium, Z::Low })
                if (kept == JPCameraView::name(z)) d.panel->view().setZoomSensitivity(z);
            d.panel->view().onZoomSensitivityChanged = [id = c.id](Z z) {
                JSettings::instance().set(JPlacerSettings::cameraZoomKey(id), std::string(JPCameraView::name(z)));
                JPlacerSettings::save();
            };
        }
        // How the picture is drawn, kept from last time.
        {
            using Q = JPCameraView::RenderingQuality;
            const std::string kept = JSettings::instance().get<std::string>(JPlacerSettings::cameraRenderingKey(c.id), "");
            for (Q q : { Q::Low, Q::High, Q::BestScale })
                if (kept == JPCameraView::name(q)) d.panel->view().setRenderingQuality(q);
            d.panel->view().onRenderingQualityChanged = [id = c.id](Q q) {
                JSettings::instance().set(JPlacerSettings::cameraRenderingKey(id), std::string(JPCameraView::name(q)));
                JPlacerSettings::save();
            };
        }
        d.panel->onSettings = [this, id = c.id] { showSetup("camera:" + id); };
        d.panel->onRunning = [this, id = c.id, device = c.device["backend"].str() == "switcher" ? c.device["camera"].str() : ""](bool running) {
            // A switcher camera stopped: its device camera need not run for it.
            if (!running && !device.empty())
                for (const CameraDock& other : m_cameras)
                    if (other.panel->camera().id == device) other.panel->setFeeding(id, false);
            lightCameras();
        };
        if (c.mount.headId.empty()) d.panel->view().onMoveNozzleHere = [this, id = c.id] { moveNozzleToCamera(id); };
        // OpenPnP's camera Properties: the preview's rate, held while the machine works, and brought forward.
        JPCameraPanel* panel = d.panel.get();
        d.panel->view().setPreviewFps(c.previewFps);
        if (c.suspendDuringTasks)
            d.panel->view().suspended = [this] { return m_cell && (m_cell->isMoving() || (jobRunning && jobRunning())); };
        if (c.autoCameraView) d.panel->view().onPictureShown = [this, panel] { bringForward(*panel); };
        // OpenPnP's Estimate Z Coordinate of Object, on a camera calibrated at two heights.
        d.panel->view().canEstimateZ = [this, panel, id = c.id] {
            return m_cell && m_cell->cameraCalibration(id, panel->view().pictureWidth(), panel->view().pictureHeight()).twoHeights();
        };
        d.panel->view().onEstimateZ = [this, panel, id = c.id, fixed = c.mount.headId.empty()] {
            const JPMountConfig* camera = nullptr;
            if (m_cell)
                for (const JPCameraConfig& cam : m_cell->config().cameras)
                    if (cam.id == id) camera = &cam.mount;
            if (!camera) return;
            const JPMountConfig mount = *camera;
            JPlacerEstimateZ::Where where = [this, fixed, mount]() -> std::optional<std::pair<double, double>> {
                const Where at = fixed ? whereIs(JPSetupForm::Tool::Nozzle) : whereIsMount(&mount);
                if (!at[0] || !at[1]) return std::nullopt;
                return std::pair { *at[0], *at[1] };
            };
            m_estimateZ.start(*panel, fixed, std::move(where), [this, id](int width, int height) {
                return m_cell ? m_cell->cameraCalibration(id, width, height) : JPCameraCalibration {};
            });
        };
        // OpenPnP's light toggle on the picture, while the camera has a light.
        if (const std::string light = c.lightActuator(); !light.empty()) {
            const auto known = m_lights.find(light);
            d.panel->view().setLight(true, known == m_lights.end() ? std::nullopt : std::optional(known->second));
            d.panel->view().onToggleLight = [this, light] { toggleLight(light); };
        }
        d.dock = std::make_unique<JDockWidget>(c.name, 0.f, 0.f, 0.f, 0.f);
        d.dock->setContent(d.panel.get());
        for (JWidget* tool : d.panel->tabTools()) d.dock->addTitleWidget(tool, JPIconButton::size());
        panels.push_back(d.panel.get());
        m_cameras.push_back(std::move(d));
    }
    for (CameraDock& d : m_cameras) m_layout.add(d.dock.get(), JPlacerLayout::Home::Cameras, d.panel->camera().shownInMultiView);
    // The first camera shown in front.
    for (CameraDock& d : m_cameras)
        if (d.panel->camera().shownInMultiView) {
            bringForward(*d.panel);
            break;
        }
    m_cameraTasks = std::make_unique<JPlacerCameraTasks>(m_window, *m_cell, std::move(panels),
                                                         [this](JPCameraPanel& p) { bringForward(p); }, m_cellPath);
    m_cameraTasks->setScripting(m_scripting);
    // Machine Setup shows it (the cell keeps it: JPlacerMachine::applySetup).
    m_cameraTasks->onCalibrated = [this](const std::string& cameraId, const JPCameraCalibration& calibration) {
        if (!m_setup) return;
        m_setup->measured([&](JPCellConfig& cell) {
            for (JPCameraConfig& c : cell.cameras)
                if (c.id == cameraId) c.keepCalibration(calibration);
        });
    };
    lightCameras();
}

void JPlacerMachine::bringForward(JPCameraPanel& camera) {
    for (CameraDock& d : m_cameras)
        if (d.panel.get() == &camera) m_layout.show(d.dock.get());
}

void JPlacerMachine::buildPanels(Keep keep) {
    const bool cameras = keep != Keep::SetupAndCameras;
    if (cameras) buildCameras();

    auto machine = std::make_unique<JPMachinePanel>(m_graph, *m_cell);
    machine->onPortChosen = [this](const std::string& driverId, const std::string& port) {
        // Posted: the choice arrives inside the panel's own event, and
        // applying it makes that panel again.
        std::weak_ptr<bool> alive = m_alive;
        JMainThreadDispatcher::instance().post([this, alive, driverId, port] {
            if (const auto a = alive.lock(); a && *a) setPort(driverId, port);
        });
    };
    using Home = JPlacerLayout::Home;
    struct Panel {
        const char*                 title;
        Home                        home;
        std::unique_ptr<JContainer> panel;
    };
    std::vector<Panel> panels;
    // The jog choices of last time.
    JPJogPanel::Choices choices;
    JSettings& settings = JSettings::instance();
    choices.tool = settings.get<std::string>(JPlacerSettings::kJogTool, "");
    choices.distance = settings.get<int>(JPlacerSettings::kJogDistance, JPJogPanel::kDistanceFirst);
    choices.speed = settings.get<double>(JPlacerSettings::kJogSpeed, JPJogPanel::kSpeedFirst);
    choices.stepThrough = settings.get<bool>(JPlacerSettings::kJogStepThrough, true);
    choices.distances = jogDistances();
    choices.speeds = jogSpeeds();
    auto jog = std::make_unique<JPJogPanel>(m_graph, *m_cell, choices);
    m_jog = jog.get();
    m_cell->setJogGuard([this](const JPMountConfig& tool, const std::map<std::string, double>& axes) { return jogSafe(tool, axes); });
    jog->onPartGone = [this](const std::string& nozzleId) { setNozzlePart(nozzleId, ""); };
    jog->onRecycle = [this](const std::string& nozzleId) {
        if (recycle) recycle(nozzleId);
    };
    jog->canRecycle = [this](const std::string& nozzleId) {
        const std::string part = nozzlePart(nozzleId);
        return !part.empty() && canRecycle && canRecycle(part);
    };
    jog->onChoicesChanged = [this] {
        if (!m_jog) return;
        const JPJogPanel::Choices c = m_jog->choices();
        JSettings::instance().set(JPlacerSettings::kJogTool, c.tool);
        JSettings::instance().set(JPlacerSettings::kJogDistance, c.distance);
        JSettings::instance().set(JPlacerSettings::kJogSpeed, c.speed);
        JSettings::instance().set(JPlacerSettings::kJogStepThrough, c.stepThrough);
        JPlacerSettings::save();
    };
    jog->onChangeTip = [this](const std::string& nozzleId, const std::string& tipId, bool everyStep) {
        if (m_tipChanges) m_tipChanges->change(nozzleId, tipId, everyStep);
    };
    jog->onTipOnIt = [this](const std::string& nozzleId, const std::string& tipId) {
        if (m_tipChanges && m_tipChanges->busy()) {
            m_window.showStatus("Wait for the tip change under way to finish or stop", kErrorMs);
            return;
        }
        setTipOn(nozzleId, tipId);
    };
    jog->onStop = [this](bool emergency) { stop(emergency); };
    jog->keyFor = [this](const std::string& action) { return keyFor ? keyFor(action) : std::string(); };
    jog->onHomeZ = [this](const std::string& nozzleId) { homeNozzle(nozzleId); };
    jog->openMenu = [this](JMenu* menu, float x, float y) {
        if (JMenuManager::instance().onOpenMenu)
            JMenuManager::instance().onOpenMenu(menu, m_window.windowX() + int(x), m_window.windowY() + int(y), false, false);
    };
    panels.push_back({ "Jog",       Home::Controls, std::move(jog) });
    panels.push_back({ "Actuators", Home::Controls, std::make_unique<JPActuatorPanel>(m_graph, *m_cell) });
    if (keep != Keep::Nothing) panels.push_back({ "Machine Setup", Home::Work, nullptr });   // kept
    else panels.push_back({ "Machine Setup", Home::Work, makeSetup() });
    panels.push_back({ "Machine",   Home::Work, std::move(machine) });
    auto console = std::make_unique<JPConsolePanel>(m_graph, *m_cell,
                                                    JSettings::instance().get<bool>(JPlacerSettings::kConsoleTraffic, true));
    console->onShowTraffic = [](bool on) {
        JSettings::instance().set(JPlacerSettings::kConsoleTraffic, on);
        JPlacerSettings::save();
    };
    console->onLogLevels = [](const JPLogLevels& levels) {
        JSettings::instance().set(JPlacerSettings::kLogLevels, levels.toText());
        JPlacerSettings::save();
    };
    console->openMenu = [this](JMenu* menu, float x, float y) {
        if (JMenuManager::instance().onOpenMenu)
            JMenuManager::instance().onOpenMenu(menu, m_window.windowX() + int(x), m_window.windowY() + int(y), false, false);
    };
    panels.push_back({ "Console",   Home::Console, std::move(console) });

    const bool first = m_docks.empty();
    for (size_t i = 0; i < panels.size(); ++i) {
        if (first) {
            Dock d;
            d.dock = std::make_unique<JDockWidget>(panels[i].title, 0.f, 0.f, 0.f, 0.f);
            m_layout.add(d.dock.get(), panels[i].home);
            m_docks.push_back(std::move(d));
        }
        if (!panels[i].panel) continue;
        m_docks[i].panel = std::move(panels[i].panel);
        m_docks[i].dock->setContent(m_docks[i].panel.get());
    }
    if (first) {
        // Each place opens on its first panel (the last added would be in front).
        showDock("Jog");
        showDock("Machine Setup");
    }
}

std::unique_ptr<JPMachineSetupPanel> JPlacerMachine::makeSetup() {
    auto setup = std::make_unique<JPMachineSetupPanel>(
        m_graph, m_cell->config(), m_profiles, m_setupSelected,
        JSettings::instance().get<double>(JPlacerSettings::kSetupTreeShare, JPMachineSetupPanel::kTreeShare));
    m_setup = setup.get();
    setup->setConfiguration(m_configuration);
    setup->setVisionTests(m_setupVisionTests);
    setup->onConfigurationChanged = [this] {
        if (onSetupConfigurationChanged) onSetupConfigurationChanged();
    };
    setup->visionAction = [this](const std::string& id, const std::string& action) {
        if (onSetupVisionAction) onSetupVisionAction(id, action);
    };
    setup->onSelected = [this](const std::string& path) { m_setupSelected = path; };
    setup->onApply = [this](const JPCellConfig& cell) { return applySetup(cell); };
    setup->onAction = [this](const std::string& path, const std::string& action) { setupAction(path, action); };
    setup->probeZ = [this](double x, double y, std::function<void(double)> done) { return probeZ && probeZ(x, y, std::move(done)); };
    setup->chooseClass = [this](const std::string& title, const std::string& description, const std::vector<std::string>& classes,
                                std::function<void(std::string)> chosen) {
        m_window.openModal<JPlacerClassSelectionDialog>(title, description, classes, std::move(chosen));
    };
    setup->whereIs = [this](JPSetupForm::Tool tool) { return whereIs(tool); };
    setup->axisAt = [this](const std::string& axisId) -> std::optional<double> {
        if (!m_cell->isConnected()) return std::nullopt;
        const auto p = m_cell->positions();
        const auto i = p.find(axisId);
        return i == p.end() ? std::nullopt : std::optional<double>(i->second);
    };
    setup->moveTo = [this](JPSetupForm::Tool tool, const JPMachineSetupPanel::Where& to) { moveToolTo(tool, to); };
    setup->moveToStraight = [this](JPSetupForm::Tool tool, const JPMachineSetupPanel::Where& to) { moveToolTo(tool, to, true); };
    setup->setHal(&m_window.hal());
    // Template pictures are named by what they hold: one read is kept.
    setup->templatePicture = [this, kept = std::make_shared<std::map<std::string, std::shared_ptr<const JPFrame>>>()](
                                 const std::string& fileName) -> std::shared_ptr<const JPFrame> {
        if (const auto it = kept->find(fileName); it != kept->end()) return it->second;
        auto frame = std::make_shared<JPFrame>();
        std::string error;
        if (!JPImageFile::readPng(JPlacerSlotVision::templatePath(m_cellPath, fileName), *frame, error)) {
            JLOGC(JPlacerLog::kUi, JLogLevel::Warn) << error;
            return nullptr;
        }
        return (*kept)[fileName] = std::move(frame);
    };
    setup->contactProbeAt = [this](const JPMachineSetupPanel::Where& at, std::function<void(double)> done) {
        return contactProbeAt(at, std::move(done));
    };
    setup->moveAxis = [this](const std::string& axisId, double to) {
        if (!readyToMove()) return;
        m_cell->moveAxes({ { axisId, to } }, 1.0);
    };
    setup->onHistory = [this] { updateEditItems(); };
    updateEditItems();
    return setup;
}

void JPlacerMachine::stop(bool emergency) {
    if (!m_cell) return;
    std::string why;
    if (!m_cell->stop(emergency, why)) {
        m_window.showStatus("Not stopped: " + why, kErrorMs);
        return;
    }
    m_window.showStatus(emergency ? "EMERGENCY STOP: every controller reset; home the machine before moving it"
                                  : "Stopped: the move held and dropped, the position kept", kErrorMs);
}

void JPlacerMachine::park() {
    if (!m_cell) return;
    for (const JPHeadConfig& h : m_cell->config().heads)
        if (h.park) {
            JLOGC(JPlacerLog::kUi, JLogLevel::Info) << "Machine: park " << h.name;
            m_cell->park(h.id, 1.0);   // at the machine's speed
            return;
        }
    m_window.showStatus("No head has a park place set", kErrorMs);
}

void JPlacerMachine::homeNozzle(const std::string& nozzleId) {
    if (!m_cell) return;
    if (m_tipChanges && m_tipChanges->busy()) {
        m_window.showStatus("Wait for the tip change under way to finish or stop", kErrorMs);
        return;
    }
    if (m_cell->isMoving()) {
        m_window.showStatus("Wait for the move under way to finish or stop", kErrorMs);
        return;
    }
    std::string names;
    for (const std::string& id : m_cell->nozzlesHomedWith(nozzleId))
        for (const JPNozzleConfig& n : m_cell->config().nozzles)
            if (n.id == id) names += (names.empty() ? "" : " and ") + n.name;
    JLOGC(JPlacerLog::kUi, JLogLevel::Info) << "Machine: home Z of " << names;
    m_window.showStatus("Homing the Z of " + names + ", from the park place", kStatusMs);
    m_cell->homeNozzle(nozzleId, 1.0);   // at the machine's speed
}

void JPlacerMachine::setCameraOverlay(const std::string& key, JPCameraView::Overlay overlay) {
    if (overlay) m_overlays[key] = overlay;
    else m_overlays.erase(key);
    for (CameraDock& c : m_cameras) c.panel->setOverlay(key, overlay);
}

std::vector<std::pair<std::string, std::string>> JPlacerMachine::nozzleTips() const {
    std::vector<std::pair<std::string, std::string>> out;
    if (m_cell)
        for (const JPNozzleTipConfig& t : m_cell->config().nozzleTips) out.emplace_back(t.id, t.name.empty() ? t.id : t.name);
    return out;
}

std::string JPlacerMachine::cellsDir() {
    return (std::filesystem::path(JPlacerPaths::configDir()) / "cells").string();
}

void JPlacerMachine::setMenuItems(JMenuItem* connect, JMenuItem* disconnect, JMenuItem* home, JMenuItem* park) {
    m_connectItem    = connect;
    m_disconnectItem = disconnect;
    m_homeItem       = home;
    m_parkItem       = park;
    updateMenu();
}

void JPlacerMachine::updateMenu() {
    const bool open = m_cell != nullptr, connected = open && m_cell->isConnected();
    if (m_connectItem)    m_connectItem->setEnabled(open && !connected);
    if (m_disconnectItem) m_disconnectItem->setEnabled(connected);
    if (m_homeItem)       m_homeItem->setEnabled(connected);
    if (m_parkItem)       m_parkItem->setEnabled(connected && m_cell->isHomed());
    if (onConnectedChanged) onConnectedChanged(connected);
}

bool JPlacerMachine::openCell(const std::string& path, std::string& error) {
    // A job works on the cell open: another waits until it has stopped.
    if (jobRunning && jobRunning()) {
        error = "a job is running: stop it first";
        return false;
    }
    JPCellConfig config;
    if (!config.load(path, error)) return false;
    for (const std::string& p : config.problems()) JLOGC(JPlacerLog::kApp, JLogLevel::Warn) << path << ": " << p;

    dropPanels();
    m_tipChanges.reset();   // a change under way stops before its cell goes
    m_testMotion.reset();
    m_cell = std::make_unique<JPCell>(std::move(config), m_profiles);
    m_cell->setScripting(m_scripting);
    m_cell->setPnpChecker(m_pnpChecking.checker());
    m_cellPath = path;
    m_testMotion = std::make_unique<JPlacerTestMotion>(m_window, *m_cell);
    m_tipChanges = std::make_unique<JPlacerTipChanges>(m_window, *m_cell, [this](const std::string& nozzleId, const std::string& tipId) {
        setTipOn(nozzleId, tipId);
    });
    watchCell();
    buildPanels();

    JSettings::instance().set(JPlacerSettings::kMachineCell, path);
    JPlacerSettings::save();
    JLOGC(JPlacerLog::kApp, JLogLevel::Info) << "cell " << m_cell->config().name << " from " << path;
    m_connecting = m_connectFailed = m_homeFailed = false;
    m_lost.clear();
    updateMenu();
    showState();
    return true;
}

void JPlacerMachine::watchCell() {
    // The menu and the strip follow the cell.
    std::weak_ptr<bool> alive = m_alive;
    auto onMain = [this, alive](std::function<void()> fn) {
        JMainThreadDispatcher::instance().post([this, alive, fn] {
            if (const auto a = alive.lock(); a && *a) {
                fn();
                updateMenu();
                showState();
            }
        });
    };
    // What each light was last switched to, for the cameras' light toggles.
    m_unwatch.push_back(m_cell->onActuator.connect([this, onMain](std::string id, bool ok, std::string value) {
        if (!ok || (value != "on" && value != "off")) return;
        onMain([this, id, value] { showLight(id, value == "on"); });
    }));
    m_unwatch.push_back(m_cell->onConnection.connect([this, onMain](bool ok, std::string why) {
        onMain([this, ok, why] {
            const bool wasConnecting = m_connecting;
            m_connecting = false;
            m_connectFailed = !ok && wasConnecting;
            if (ok) m_lost.clear();
            // Homed straight away, when the machine is set to (after a connect asked for here).
            if (ok && wasConnecting && m_cell->config().homeAfterConnect()) home();
            else if (!wasConnecting && !why.empty()) m_lost = why;   // dropped while working
            // A camera on screen gets its light as soon as there is a
            // machine to switch it; without one, its panel says why it is dark.
            if (ok) for (CameraDock& c : m_cameras) if (!c.panel->camera().lightActuator().empty()) c.panel->setNote("");
            // Not connected, no light's state is known.
            if (!ok)
                for (CameraDock& c : m_cameras)
                    if (const std::string light = c.panel->camera().lightActuator(); !light.empty()) showLight(light, std::nullopt);
            lightCameras();
            if (!why.empty()) m_window.showStatus(why, kErrorMs);
            else m_window.showStatus(ok ? m_cell->config().name + " connected" : m_cell->config().name + " disconnected", kStatusMs);
        });
    }));
    // A home by the switches is finished with the camera where the head homes visually.
    // A new calibration straightens the picture from then on.
    m_unwatch.push_back(m_cell->onCalibration.connect([this, onMain] {
        onMain([this] {
            for (CameraDock& c : m_cameras) c.panel->refreshStraightening();
        });
    }));
    // What each controller said it is (Detect Firmware, and at connect), shown on its page.
    auto showFirmware = [this] {
        if (!m_setup || !m_cell) return;
        const auto said = m_cell->firmwareIdentity();
        m_setup->measured([&](JPCellConfig& cell) {
            for (JPDriverConfig& d : cell.drivers)
                if (const auto it = said.find(d.id); it != said.end()) d.detectedFirmware = it->second;
        });
    };
    m_unwatch.push_back(m_cell->onFirmwareDetected.connect([onMain, showFirmware](std::string) { onMain(showFirmware); }));
    m_unwatch.push_back(m_cell->onConnection.connect([onMain, showFirmware](bool ok, std::string) {
        if (ok) onMain(showFirmware);
    }));
    // A tip's vacuum readings and graph, shown on its Part Detection tab.
    m_unwatch.push_back(m_cell->onVacuumReadings.connect([this, onMain](std::string tipId) {
        onMain([this, tipId] {
            if (!m_setup || !m_cell) return;
            JPNozzleTipConfig::Sensing on, off;
            m_cell->vacuumReadings(tipId, on, off);
            m_setup->measured([&](JPCellConfig& cell) {
                for (JPNozzleTipConfig& t : cell.nozzleTips) {
                    if (t.id != tipId) continue;
                    for (auto [from, into] : { std::pair { &on, &t.partOn }, std::pair { &off, &t.partOff } }) {
                        into->lastReading = from->lastReading;
                        into->lastDifference = from->lastDifference;
                        into->vacuumGraph = from->vacuumGraph;
                        into->valveGraph = from->valveGraph;
                    }
                }
            });
        });
    }));
    m_unwatch.push_back(m_cell->onHomed.connect([this, onMain](bool homed) {
        onMain([this, homed] {
            if (!homed && onUnhomed) onUnhomed();
            // As OpenPnP's: Machine.AfterDriverHoming once the controllers have homed, then the head's
            // (visual) homing, Machine.AfterHoming, and the park.
            if (!homed) return;
            auto afterHoming = [this] {
                runEvent("Machine.AfterHoming", [this] {
                    if (m_cell && m_cell->isHomed() && m_cell->config().parkAfterHome) park();
                });
            };
            // The nozzle tips' runout recalibrated as their Auto Recalibration says, before Machine.AfterHoming.
            auto recalibrate = [this, afterHoming] {
                std::vector<std::string> nozzles;
                if (m_cell)
                    for (const JPNozzleConfig& n : m_cell->config().nozzles) nozzles.push_back(n.id);
                recalibrateAfterHoming(std::move(nozzles), [afterHoming](bool ok) {
                    if (ok) afterHoming();
                });
            };
            runEvent("Machine.AfterDriverHoming", [this, recalibrate] {
                if (!m_cell || !m_cell->isHomed()) return;
                if (m_cameraTasks)
                    m_cameraTasks->visualHome([recalibrate](bool ok) {
                        if (ok) recalibrate();
                    });
                else
                    recalibrate();
            });
        });
    }));
    m_unwatch.push_back(m_cell->onState.connect([onMain](std::string, std::string) { onMain([] {}); }));
    m_unwatch.push_back(m_cell->onMotion.connect([this, onMain](bool ok, std::string why) {
        onMain([this, ok, why] {
            if (m_cell->isHomed() || ok) m_homeFailed = false;
            // A camera moved to look somewhere: OpenPnP's Camera.AfterPosition once it is there.
            if (!m_positionedCamera.empty()) {
                const std::string camera = std::exchange(m_positionedCamera, std::string());
                JJson g = JJson::object();
                g["camera"] = camera;
                if (ok) runEvent("Camera.AfterPosition", nullptr, g);
            }
            if (!ok && !why.empty()) {
                if (!m_cell->isHomed()) m_homeFailed = true;
                m_window.showStatus(why, kErrorMs);
            }
        });
    }));
}

void JPlacerMachine::setPort(const std::string& driverId, const std::string& port) {
    const JPDriverConfig* d = m_cell->config().driver(driverId);
    if (!m_setup || !d || d->link["port"].str() == port) return;
    JLOGC(JPlacerLog::kCell, JLogLevel::Info) << m_cellPath << ": controller " << driverId << " now on " << port;
    // A step in Machine Setup like any other, so Undo takes it back.
    m_setup->change("Port of " + d->name, [&](JPCellConfig& cell) {
        for (JPDriverConfig& c : cell.drivers)
            if (c.id == driverId) c.link["port"] = port;
    });
}

bool JPlacerMachine::applySetup(JPCellConfig cell) {
    if (!m_cell) return false;
    // Measured while it was being set up: the cell's, not the copy's.
    for (JPCameraConfig& c : cell.cameras) c.calibrations = m_cell->cameraCalibrations(c.id);
    cell.squareness = m_cell->squareness();
    // The cameras are made again only
    // when what they show changed: where they are, and how they are set up.
    auto seen = [](const JPCellConfig& c) {
        JJson j = JJson::array();
        for (const JPCameraConfig& x : c.cameras) j.push(x.toJson());
        for (const JPHeadConfig& x : c.heads) j.push(x.toJson());
        for (const JPAxisConfig& x : c.axes) j.push(x.toJson());
        return j.dump();
    };
    const Keep keep = seen(cell) == seen(m_cell->config()) ? Keep::SetupAndCameras : Keep::Setup;
    // The running machine takes the new settings (JPCell::reconfigure).
    std::string error;
    if (m_cell->isMoving() || m_cell->isHoming()) {
        m_window.showStatus("Machine Setup's changes are taken once the machine stops", kStatusMs);
        return false;
    }
    dropPanels(keep);
    const bool taken = m_cell->reconfigure(cell, error);
    watchCell();
    buildPanels(keep);
    updateMenu();
    showState();
    if (!taken) {
        m_window.showStatus("Machine Setup's changes are not in use: " + error, kErrorMs);
        return false;
    }
    if (!cell.save(m_cellPath, error)) {
        m_window.showStatus("Machine Setup's changes are in use but not saved: " + error, kErrorMs);
        return true;
    }
    JLOGC(JPlacerLog::kUi, JLogLevel::Info) << "Machine Setup: in use and saved to " << m_cellPath;
    return true;
}

const JPMountConfig* JPlacerMachine::toolMount(JPSetupForm::Tool tool) const {
    if (!m_cell) return nullptr;
    const JPCellConfig& c = m_cell->config();
    if (tool == JPSetupForm::Tool::Camera) {
        // The camera the board is worked with, else any on a head.
        if (m_cameraTasks)
            if (const JPCameraPanel* p = m_cameraTasks->headCamera())
                for (const JPCameraConfig& cam : c.cameras)
                    if (cam.id == p->camera().id) return &cam.mount;
        for (const JPCameraConfig& cam : c.cameras)
            if (!cam.mount.headId.empty()) return &cam.mount;
        return nullptr;
    }
    // The nozzle chosen on the Jog panel, else the first.
    const std::string chosen = m_jog ? m_jog->toolId() : std::string();
    for (const JPNozzleConfig& n : c.nozzles)
        if (n.id == chosen) return &n.mount;
    return c.nozzles.empty() ? nullptr : &c.nozzles.front().mount;
}

JPlacerMachine::Where JPlacerMachine::whereIsActuator(const std::string& name) const {
    const JPActuatorConfig* a = m_cell ? m_cell->config().actuatorNamed(name) : nullptr;
    return a && !a->mount.headId.empty() ? whereIsMount(&a->mount) : Where {};
}

bool JPlacerMachine::moveActuatorTo(const std::string& name, const Where& to, bool straight) {
    const JPActuatorConfig* a = m_cell ? m_cell->config().actuatorNamed(name) : nullptr;
    if (!a || a->mount.headId.empty()) {
        m_window.showStatus("No Actuator with name " + name + " on the head", kErrorMs);
        return false;
    }
    if (!readyToMove()) return false;
    if (straight) m_cell->moveToolStraight(a->mount, to, 1.0);
    else m_cell->moveTool(a->mount, to, 1.0);
    selectMoved(a->mount);
    return true;
}

JPCameraView* JPlacerMachine::headCameraView() {
    JPCameraPanel* p = m_cameraTasks ? m_cameraTasks->headCamera() : nullptr;
    if (!p) return nullptr;
    showCamera(p->camera().id);
    return &p->view();
}

std::string JPlacerMachine::nozzlePart(const std::string& nozzleId) const {
    const auto it = m_nozzleParts.find(nozzleId);
    return it == m_nozzleParts.end() ? std::string() : it->second;
}

void JPlacerMachine::setNozzlePart(const std::string& nozzleId, const std::string& partId) {
    if (partId.empty()) m_nozzleParts.erase(nozzleId);
    else m_nozzleParts[nozzleId] = partId;
    // Its height (the nozzle's Dynamic Safe Z), and its package's pick vacuum and place blow-off levels.
    JPCell::PartOnNozzle on;
    on.partId = partId;
    std::shared_ptr<const JPFootprint> footprint;   // for Simulation Mode's Pick & Place Checking
    if (const JPPart* part = m_configuration && !partId.empty() ? m_configuration->part(partId) : nullptr) {
        on.heightMm = part->heightForSafeZ().convertToUnits(JPLengthUnit::Millimeters).value();
        // A height not known: the nozzle's tip's Max. Part Height (OpenPnP's getSafePartHeight).
        if (part->height.value() <= 0 && m_cell)
            for (const JPNozzleConfig& n : m_cell->config().nozzles)
                if (n.id == nozzleId)
                    for (const JPNozzleTipConfig& t : m_cell->config().nozzleTips)
                        if (t.id == n.tipId) on.heightMm = t.maxPartHeightMm;
        if (const JPPackage* pkg = m_configuration->package(part->packageId)) {
            on.pickVacuumLevel = pkg->pickVacuumLevel;
            on.placeBlowOffLevel = pkg->placeBlowOffLevel;
            footprint = std::make_shared<const JPFootprint>(pkg->footprint);
        }
    }
    m_pnpChecking.hold(nozzleId, std::move(footprint));
    if (m_cell) m_cell->setNozzlePart(nozzleId, on);
    if (m_jog) m_jog->refreshRecycle();
}

void JPlacerMachine::setSetupVisionTests(JPVisionTests tests) {
    m_setupVisionTests = std::move(tests);
    if (m_setup) m_setup->setVisionTests(m_setupVisionTests);
}

void JPlacerMachine::refreshSetupForm() {
    if (m_setup) m_setup->refreshForm();
}

void JPlacerMachine::setConfiguration(JPConfiguration* config) {
    m_configuration = config;
    if (m_setup) m_setup->setConfiguration(config);
}

JPCameraView* JPlacerMachine::cameraViewOf(const JPCameraFeed* feed) {
    for (const CameraDock& d : m_cameras)
        if (&d.panel->feed() == feed) {
            showCamera(d.panel->camera().id);
            return &d.panel->view();
        }
    return nullptr;
}

JPlacerMachine::Where JPlacerMachine::whereIs(JPSetupForm::Tool tool) const {
    return whereIsMount(toolMount(tool));
}

JPlacerMachine::Where JPlacerMachine::whereIsMount(const JPMountConfig* m) const {
    Where at;
    if (!m || !m_cell || !m_cell->isConnected()) return at;
    const auto p = m_cell->positions();
    auto take = [&p](const std::string& axis, double offset) -> std::optional<double> {
        const auto i = p.find(axis);
        if (axis.empty() || i == p.end()) return std::nullopt;
        return i->second + offset;
    };
    return { take(m->axisX, m->offsetX), take(m->axisY, m->offsetY), take(m->axisZ, m->offsetZ), take(m->axisRotation, 0) };
}

std::optional<JPLocation> JPlacerMachine::toolLocation(JPSetupForm::Tool tool) const {
    const Where at = whereIs(tool);
    if (!at[0] || !at[1]) return std::nullopt;
    return JPLocation(JPLengthUnit::Millimeters, *at[0], *at[1], at[2].value_or(0), at[3].value_or(0));
}

bool JPlacerMachine::moveToolTo(JPSetupForm::Tool tool, const Where& to, bool straight) {
    const JPMountConfig* m = toolMount(tool);
    if (!m) {
        m_window.showStatus(tool == JPSetupForm::Tool::Camera ? "No camera on a head to move" : "No nozzle to move", kErrorMs);
        return false;
    }
    if (!readyToMove()) return false;
    if (straight) m_cell->moveToolStraight(*m, to, 1.0);
    else m_cell->moveTool(*m, to, 1.0);   // at the machine's speed
    selectMoved(*m);
    if (tool == JPSetupForm::Tool::Camera)
        for (const JPCameraConfig& cam : m_cell->config().cameras)
            if (&cam.mount == m) m_positionedCamera = cam.name;
    // A camera moved to look somewhere, with Auto Camera View: brought forward.
    for (const JPCameraConfig& cam : m_cell->config().cameras)
        if (&cam.mount == m && cam.autoCameraView)
            for (CameraDock& c : m_cameras)
                if (c.panel->camera().id == cam.id) bringForward(*c.panel);
    return true;
}

bool JPlacerMachine::jogSafe(const JPMountConfig& tool, const std::map<std::string, double>& axes) {
    if (!m_jog || !m_jog->boardProtection() || !m_cell || !jobBoards) return true;
    const JPCellConfig& c = m_cell->config();
    struct Mountable {
        std::string          subject;
        const JPMountConfig* mount;
        double               safeDistance = 1;   // mm
        double               partHeight = 0;
    };
    std::vector<Mountable> on;
    for (const JPNozzleConfig& n : c.nozzles) {
        if (n.mount.headId != tool.headId) continue;
        Mountable m { "ReferenceNozzle " + n.name, &n.mount };
        const std::string partId = nozzlePart(n.id);
        for (const JPNozzleTipConfig& t : c.nozzleTips) {
            if (t.id != n.tipId) continue;
            // Half the largest part it may hold when it holds one wider than the tip, else half the tip.
            m.safeDistance += (!partId.empty() && t.maxPartDiameterMm > t.diameterLowMm ? t.maxPartDiameterMm : t.diameterLowMm) / 2;
            m.subject += " with " + (t.name.empty() ? t.id : t.name);
        }
        if (!partId.empty()) {
            if (const JPPart* part = m_configuration ? m_configuration->part(partId) : nullptr)
                m.partHeight = part->heightForSafeZ().convertToUnits(JPLengthUnit::Millimeters).value();
            m.subject += " holding " + partId;
        }
        on.push_back(m);
    }
    for (const JPActuatorConfig& a : c.actuators)
        if (a.mount.headId == tool.headId && !a.mount.axisZ.empty()) on.push_back({ "ReferenceActuator " + a.name, &a.mount });
    for (const Mountable& m : on) {
        const auto x = axes.find(m.mount->axisX), y = axes.find(m.mount->axisY), z = axes.find(m.mount->axisZ);
        if (x == axes.end() || y == axes.end() || z == axes.end()) continue;
        // Only below safe Z (a board above it is taken as not set up yet).
        const JPAxisConfig* zAxis = c.axis(m.mount->axisZ);
        if (!zAxis || zAxis->kind == JPAxisConfig::Kind::Virtual || m_cell->inSafeZone(m.mount->axisZ, z->second)) continue;
        const double px = x->second + m.mount->offsetX, py = y->second + m.mount->offsetY;
        const double pz = z->second + m.mount->offsetZ - m.partHeight;
        for (const JPBoardLocation* b : jobBoards()) {
            if (!b || !b->isEnabled() || !b->board()) continue;
            const JPLocation origin = b->globalLocation().convertToUnits(JPLengthUnit::Millimeters);
            const JPLocation size = b->board()->dimensions.convertToUnits(JPLengthUnit::Millimeters);
            // In the board's own coordinates: outside its box by the safe distance, or above it.
            const JPLocation local = JPLocation(JPLengthUnit::Millimeters, px - origin.x(), py - origin.y(), pz - origin.z(), 0)
                                         .rotateXy(-origin.rotation());
            const double d = m.safeDistance;
            if (local.x() <= -d || local.y() <= -d || local.x() >= size.x() + d || local.y() >= size.y() + d || local.z() > 0)
                continue;
            m_window.showStatus(m.subject + " would potentially crash into board " + b->id + ". "
                                    + "To disable the board protection go to the \"Safety\" tab in the \"Machine Controls\" panel.",
                                kErrorMs);
            return false;
        }
    }
    return true;
}

const JPNozzleConfig* JPlacerMachine::probingNozzle() const {
    // OpenPnP's ContactProbeNozzle.getDefaultNozzle: the first that probes by contact.
    if (!m_cell) return nullptr;
    for (const JPNozzleConfig& n : m_cell->config().nozzles)
        if (n.contactProbe.on()) return &n;
    return nullptr;
}

bool JPlacerMachine::contactProbeAt(const Where& at, std::function<void(double z)> done) {
    if (!readyToMove() || !at[0] || !at[1] || !at[2]) return false;
    // A touch location is OpenPnP's contact probe reference: asked first, probed with the default probing
    // nozzle, its Z calibration forgotten.
    const JPNozzleConfig* n = probingNozzle();
    if (!n) {
        m_window.showStatus("No default ContactProbeNozzle found.", kErrorMs);
        return false;
    }
    const std::string id = n->id, name = n->name;
    const JPMachineLocation to { *at[0], *at[1], *at[2], at[3].value_or(0) };
    std::weak_ptr<bool> alive = m_alive;
    JDialog::confirm("Select an Option",
                     "This will overwrite the Z reference and therefore change the meaning of previously captured Z "
                     "coordinates.\nYou will need to recapture these locations!\n\nAre you sure?",
                     [this, id, name, to, done = std::move(done), alive] {
                         if (const auto a = alive.lock(); !a || !*a || !m_cell || !readyToMove()) return;
                         JPCell* cell = m_cell.get();
                         std::thread([this, cell, id, name, to, done, alive] {
                             std::string why;
                             double z = 0;
                             const bool ok = cell->contactProbeCycleAndWait(id, to, true, z, why);
                             JMainThreadDispatcher::instance().post([this, ok, z, why, name, done, alive] {
                                 if (const auto a = alive.lock(); !a || !*a) return;
                                 if (!ok) {
                                     m_window.showStatus("Contact probe with " + name + " failed: " + why, kErrorMs);
                                     return;
                                 }
                                 done(z);
                             });
                         }).detach();
                     });
    return true;
}

void JPlacerMachine::slotScored(const std::string& tipId, double score) {
    if (!m_setup) return;
    m_setup->measured([&](JPCellConfig& cell) {
        for (JPNozzleTipConfig& t : cell.nozzleTips)
            if (t.id == tipId) t.visionCalibration.lastScore = score;
    });
}

void JPlacerMachine::slotVisionAction(const std::string& tipId, const std::string& action) {
    // OpenPnP's Vision Calibration buttons on a nozzle tip's Tool Changer tab, on the tip as set up.
    if (!m_cell || !m_setup) return;
    const bool empty = action.find("Empty") != std::string::npos;
    if (action == "resetSlotEmpty" || action == "resetSlotOccupied") {
        m_setup->change(empty ? "Reset Template Empty" : "Reset Template Occupied", [tipId, empty](JPCellConfig& cell) {
            for (JPNozzleTipConfig& t : cell.nozzleTips)
                if (t.id == tipId) (empty ? t.visionCalibration.templateEmpty : t.visionCalibration.templateOccupied).clear();
        });
        m_setup->remakeForm();   // its picture gone
        return;
    }
    std::optional<JPNozzleTipConfig> tip;
    for (const JPNozzleTipConfig& t : m_cell->config().nozzleTips)
        if (t.id == tipId) tip = t;
    if (!tip || !readyToMove()) return;
    JPlacerJobMachine* jm = scriptJobMachine ? scriptJobMachine() : nullptr;
    if (!jm) return;
    // OpenPnP's getNozzleWhereLoaded: in its slot when on no nozzle.
    bool onNozzle = false;
    for (const JPNozzleConfig& n : m_cell->config().nozzles) onNozzle = onNozzle || n.tipId == tipId;
    JPCell* cell = m_cell.get();
    std::weak_ptr<bool> alive = m_alive;
    std::thread([this, cell, jm, tip = *tip, action, empty, occupied = !onNozzle, cellPath = m_cellPath, alive] {
        std::string why, fileName;
        std::optional<double> score;
        std::array<double, 2> offset {};
        JPlacerSlotVision vision(*jm, *cell, cellPath);
        bool ok;
        if (action == "testSlotVision") {
            // OpenPnP's Test: found afresh, then forgotten again.
            cell->setSlotOffset(tip.id, std::nullopt);
            ok = vision.calibrate(tip, true, occupied, offset, score, why);
            cell->setSlotOffset(tip.id, std::nullopt);
        } else {
            ok = vision.captureTemplate(tip, fileName, why);
        }
        JMainThreadDispatcher::instance().post([this, ok, why, fileName, score, offset, tipId = tip.id, action, empty, alive] {
            if (const auto a = alive.lock(); !a || !*a || !m_setup) return;
            if (!ok) {
                m_window.showStatus(why, kErrorMs);
                return;
            }
            if (action == "testSlotVision") {
                if (score) slotScored(tipId, *score);
                char text[160];
                std::snprintf(text, sizeof text, "Changer slot found %.3f, %.3f mm off (%.3f mm)%s", offset[0], offset[1],
                              std::hypot(offset[0], offset[1]), score ? (", score " + std::to_string(*score)).c_str() : "");
                m_window.showStatus(text, kErrorMs);
                return;
            }
            m_setup->change(empty ? "Capture Template Empty" : "Capture Template Occupied", [tipId, empty, fileName](JPCellConfig& cell) {
                for (JPNozzleTipConfig& t : cell.nozzleTips)
                    if (t.id == tipId) (empty ? t.visionCalibration.templateEmpty : t.visionCalibration.templateOccupied) = fileName;
            });
            m_setup->remakeForm();   // its picture shown
        });
    }).detach();
}

void JPlacerMachine::referenceAllTouchLocationsZ() {
    // OpenPnP's ContactProbeNozzle.referenceAllTouchLocationsZ: the template tip loaded on the default probing
    // nozzle and its Z calibrated at its own touch location; then every other tip's touch location (one set,
    // not on a stand-in for no tip) probed with it, and its Z set to where it was met. Locked tips too.
    if (!m_cell) return;
    const JPCellConfig& c = m_cell->config();
    const JPNozzleTipConfig* templ = nullptr;
    for (const JPNozzleTipConfig& t : c.nozzleTips)
        if (t.templateTip) templ = &t;
    if (!templ) {
        m_window.showStatus("No nozzle tip is marked as Template.", kErrorMs);
        return;
    }
    const JPNozzleConfig* n = probingNozzle();
    if (!n) {
        m_window.showStatus("No default ContactProbeNozzle found.", kErrorMs);
        return;
    }
    if (!readyToMove()) return;
    JPlacerJobMachine* jm = scriptJobMachine ? scriptJobMachine() : nullptr;
    if (!jm) return;
    struct Touch {
        std::string       tipId, name;
        JPMachineLocation at;
    };
    std::vector<Touch> touches;
    for (const JPNozzleTipConfig& t : c.nozzleTips) {
        const std::string name = t.name.empty() ? t.id : t.name;
        if (t.id == templ->id || !t.touchLocation || name.rfind("unloaded", 0) == 0) continue;
        const JPMachineLocation& l = *t.touchLocation;
        if (l.x == 0 && l.y == 0 && l.z == 0) continue;
        touches.push_back({ t.id, name, l });
    }
    JPCell* cell = m_cell.get();
    std::weak_ptr<bool> alive = m_alive;
    std::thread([this, cell, jm, nozzleId = n->id, headId = n->mount.headId, onIt = n->tipId, templateId = templ->id,
                 touches, alive] {
        std::string why;
        std::vector<std::pair<std::string, double>> found;   // (tip, Z)
        bool ok = (onIt == templateId || jm->changeTip(nozzleId, templateId, why)) && cell->calibrateZAndWait(nozzleId, why);
        for (const Touch& t : touches) {
            if (!ok) break;
            double z = 0;
            ok = cell->contactProbeCycleAndWait(nozzleId, t.at, false, z, why);
            if (!ok) break;
            JLOGC(JPlacerLog::kCell, JLogLevel::Info) << "Nozzle tip " << t.name << " touch location Z set to " << z
                                                      << " (previously " << t.at.z << ")";
            found.push_back({ t.tipId, z });
        }
        if (ok) ok = cell->safeZAndWait(headId, 1.0, why);
        JMainThreadDispatcher::instance().post([this, ok, why, found, alive] {
            if (const auto a = alive.lock(); !a || !*a) return;
            if (!found.empty() && m_setup)
                m_setup->change("Calibrate all Touch Locations' Z to Template", [found](JPCellConfig& cell) {
                    for (JPNozzleTipConfig& t : cell.nozzleTips)
                        for (const auto& [id, z] : found)
                            if (t.id == id && t.touchLocation) t.touchLocation->z = z;
                });
            if (!ok) m_window.showStatus("Calibrate all Touch Locations' Z to Template failed: " + why, kErrorMs);
        });
    }).detach();
}

bool JPlacerMachine::moveToolTo(JPSetupForm::Tool tool, const JPLocation& to) {
    const JPLocation at = to.convertToUnits(JPLengthUnit::Millimeters);
    return moveToolTo(tool, Where { at.x(), at.y(), at.z(), at.rotation() });
}

void JPlacerMachine::selectMoved(const JPMountConfig& mount) {
    if (!m_jog || !m_cell || !m_cell->config().autoToolSelect) return;
    const JPCellConfig& c = m_cell->config();
    auto idOf = [&mount](const auto& tools) -> std::string {
        for (const auto& t : tools)
            if (&t.mount == &mount) return t.id;
        return {};
    };
    for (const std::string& id : { idOf(c.nozzles), idOf(c.cameras), idOf(c.actuators) })
        if (!id.empty()) m_jog->selectTool(id);
}

bool JPlacerMachine::readyToMove() {
    if (!m_cell || !m_cell->isConnected()) {
        m_window.showStatus("Connect the machine first", kErrorMs);
        return false;
    }
    if (!m_cell->isHomed()) {
        m_window.showStatus("Home the machine first", kErrorMs);
        return false;
    }
    return true;
}

void JPlacerMachine::setupAction(const std::string& path, const std::string& action) {
    if (!m_cameraTasks) return;
    if (action == "testMotion" && m_cell) {
        // The tool chosen on the Jog panel: a nozzle or a camera, else the first nozzle.
        const std::string chosen = m_jog ? m_jog->toolId() : std::string();
        const JPMountConfig* tool = nullptr;
        for (const JPCameraConfig& c : m_cell->config().cameras)
            if (c.id == chosen) tool = &c.mount;
        if (!tool) tool = toolMount(JPSetupForm::Tool::Nozzle);
        if (!tool) {
            m_window.showStatus("Test Motion: no nozzle or camera to move", kErrorMs);
            return;
        }
        if (m_testMotion)
            m_testMotion->run(*tool, [this](const JPMotionTestResult& r) {
                if (m_setup) m_setup->setMotionTest(r);
            });
        return;
    }
    if (action == "resetFeeders" && m_configuration) {
        // OpenPnP's Simulation Mode Reset Feeders: the strip and blinds feeders from their first part again.
        int reset = 0;
        for (JPFeeder& feeder : m_configuration->feeders()) {
            const std::string kind = JPFeeder::simpleName(feeder.className());
            if (kind != "ReferenceStripFeeder" && kind != "BlindsFeeder") continue;
            feeder.setNumber("feed-count", 0);
            ++reset;
        }
        if (onSetupConfigurationChanged) onSetupConfigurationChanged();
        m_window.showStatus("Reset Feeders: " + std::to_string(reset) + " feeder(s) back to their first part", kStatusMs);
        return;
    }
    if (action == "setMachineTableZ" && m_cell && m_configuration) {
        // OpenPnP's Set Machine Table Z: the feeders (a strip feeder's holes too), the job's boards and the cameras.
        const double z = m_cell->config().simulation.machineTableZ;
        auto atZ = [z](const JPLocation& l) {
            return l.derive(std::nullopt, std::nullopt, z / JPLengthUnits::toMillimeters(l.units()), std::nullopt);
        };
        for (JPFeeder& feeder : m_configuration->feeders())
            for (const char* element : { "location", "reference-hole-location", "last-hole-location" })
                if (feeder.has(element)) feeder.setLocationOf(element, atZ(feeder.locationOf(element)));
        if (onSetupConfigurationChanged) onSetupConfigurationChanged();
        if (setBoardsZ) setBoardsZ(z);
        changeSetup("Set Machine Table Z", [z](JPCellConfig& cell) {
            for (JPCameraConfig& c : cell.cameras) c.mount.offsetZ = z;
        });
        m_window.showStatus("Machine Table Z " + JPUiParts::coordinate(z) + " given to the feeders, the boards and the cameras",
                            kStatusMs);
        return;
    }
    if (action == "visualTest") {
        if (JPCameraPanel* camera = m_cameraTasks->headCamera()) m_cameraTasks->visualTest(*camera);
        else m_window.showStatus("No camera on a head to test with", kErrorMs);
    } else if (action == "visualHome") {
        m_cameraTasks->visualHome();
    } else if (action == "calibrate" && path.rfind("camera:", 0) == 0) {
        for (CameraDock& c : m_cameras)
            if ("camera:" + c.panel->camera().id == path) m_cameraTasks->calibrate(*c.panel);
    } else if ((action == "storeNozzleMark" || action == "calculateNozzleOffset") && path.rfind("nozzle:", 0) == 0) {
        nozzleOffsetWizard(path.substr(7), action == "storeNozzleMark");
    } else if (action.rfind("settleTest", 0) == 0 && path.rfind("camera:", 0) == 0) {
        // One jog step (the Jog pad's distance) out and back, or none.
        const std::string id = path.substr(7);
        const double step = m_jog ? m_jog->lengthStep() : 0;
        JPlacerCameraTasks::SettleMove move;
        move.dx = action == "settleTestLeft" ? -step : action == "settleTestRight" ? step : 0;
        move.dy = action == "settleTestFront" ? -step : action == "settleTestBack" ? step : 0;
        move.dc = action == "settleTestRotate" && m_jog ? m_jog->distance() : 0;
        move.up = action == "settleTestUp";
        for (CameraDock& c : m_cameras)
            if (c.panel->camera().id == id)
                m_cameraTasks->settleTest(*c.panel, toolMount(JPSetupForm::Tool::Nozzle), move, [this, id](const JPSettleTrace& trace) {
                    if (!m_setup) return;
                    m_setup->measured([&](JPCellConfig& cell) {
                        for (JPCameraConfig& cam : cell.cameras)
                            if (cam.id == id) cam.settleTrace = trace;
                    });
                });
    } else if (action == "detectFirmware" && path.rfind("driver:", 0) == 0) {
        m_cell->detectFirmware(path.substr(7));
    } else if (action == "positionRunoutTool" && path.rfind("nozzletip:", 0) == 0) {
        // OpenPnP's Position Tool: the nozzle the tip is on over the camera looking up, where the tip is calibrated.
        const std::string tipId = path.substr(10);
        const JPNozzleConfig* on = nullptr;
        for (const JPNozzleConfig& n : m_cell->config().nozzles)
            if (n.tipId == tipId) on = &n;
        const JPCameraFeed* up = upCameraFeed();
        const JPNozzleTipConfig* tip = nullptr;
        for (const JPNozzleTipConfig& t : m_cell->config().nozzleTips)
            if (t.id == tipId) tip = &t;
        if (!on || !up || !tip) {
            m_window.showStatus(!on ? "Load the tip on a nozzle first" : "No camera looking up", kErrorMs);
            return;
        }
        if (!readyToMove()) return;
        const JPMountConfig& cam = up->config().mount;
        m_cell->moveTool(on->mount, { cam.offsetX, cam.offsetY, cam.offsetZ + tip->runoutCalibration.zOffset, std::nullopt }, 1.0);
        selectMoved(on->mount);
    } else if ((action == "calibrateRunout" || action == "resetRunout") && path.rfind("nozzletip:", 0) == 0) {
        // On the nozzle the tip is on; kept through Machine Setup, a step to undo.
        const std::string tipId = path.substr(10);
        const JPNozzleConfig* on = nullptr;
        for (const JPNozzleConfig& n : m_cell->config().nozzles)
            if (n.tipId == tipId) on = &n;
        if (!on) {
            m_window.showStatus("Load the tip on a nozzle first: its runout is measured on that nozzle", kErrorMs);
            return;
        }
        if (action == "resetRunout") {
            keepRunout(tipId, on->id, std::nullopt);
            return;
        }
        calibrateTipRunout(on->id, true, nullptr);
    } else if (action == "calibrateNozzleOffsets" && path.rfind("nozzle:", 0) == 0) {
        const std::string nozzleId = path.substr(7);
        JPCameraPanel* camera = m_cameraTasks->headCamera();
        const JPNozzleConfig* nozzle = nullptr;
        for (const JPNozzleConfig& n : m_cell->config().nozzles)
            if (n.id == nozzleId) nozzle = &n;
        if (!camera || !nozzle) {
            m_window.showStatus("Nozzle offsets: a camera on the head and the nozzle are needed", kErrorMs);
            return;
        }
        m_cameraTasks->calibrateNozzleOffsets(*camera, *nozzle, [this, nozzleId](double dx, double dy) {
            if (!m_setup) return;
            m_setup->change("Precise nozzle offsets", [&](JPCellConfig& cell) {
                for (JPNozzleConfig& n : cell.nozzles)
                    if (n.id == nozzleId) {
                        JLOGC(JPlacerLog::kCamera, JLogLevel::Info) << "Set nozzle " << n.name << " head offsets to "
                            << n.mount.offsetX + dx << ", " << n.mount.offsetY + dy << " (previously " << n.mount.offsetX
                            << ", " << n.mount.offsetY << ")";
                        n.mount.offsetX += dx;
                        n.mount.offsetY += dy;
                    }
            });
            m_setup->remakeForm();
        });
    } else if ((action == "autoFocusTest" || action == "adjustCameraZ") && path.rfind("camera:", 0) == 0) {
        const std::string cameraId = path.substr(7);
        const JPMountConfig* nozzleMount = toolMount(JPSetupForm::Tool::Nozzle);
        const JPNozzleConfig* nozzle = nullptr;
        for (const JPNozzleConfig& n : m_cell->config().nozzles)
            if (&n.mount == nozzleMount) nozzle = &n;
        if (!nozzle) {
            m_window.showStatus("No nozzle chosen on the Jog panel", kErrorMs);
            return;
        }
        if (action == "autoFocusTest") {
            for (CameraDock& c : m_cameras)
                if (c.panel->camera().id == cameraId)
                    m_cameraTasks->autoFocusTest(*c.panel, *nozzle, [this, cameraId](double distance) {
                        if (!m_setup) return;
                        m_setup->measured([&](JPCellConfig& cell) {
                            for (JPCameraConfig& cam : cell.cameras)
                                if (cam.id == cameraId) cam.lastFocusDistanceMm = distance;
                        });
                    });
            return;
        }
        // OpenPnP's Adjust Camera Z: the camera's Z made the nozzle's, once it is over the camera and focused.
        const JPCameraConfig* cam = nullptr;
        for (const JPCameraConfig& c : m_cell->config().cameras)
            if (c.id == cameraId) cam = &c;
        const auto at = toolLocation(JPSetupForm::Tool::Nozzle);
        if (!cam || !at) return;
        if (!nozzlePart(nozzle->id).empty()) {
            m_window.showStatus("Nozzle " + nozzle->name + " has part on. Use nozzle tip to measure.", kErrorMs);
            return;
        }
        const JPLocation l = at->convertToUnits(JPLengthUnit::Millimeters);
        if (std::hypot(l.x() - cam->mount.offsetX, l.y() - cam->mount.offsetY) > kCenteredMm) {
            m_window.showStatus("Nozzle " + nozzle->name + " unexpected location. Please center and focus first.", kErrorMs);
            return;
        }
        const double z = l.z();
        JDialogOptions opts;
        opts.okLabel = "Yes";
        opts.cancelLabel = "No";
        JDialog::confirm("Adjust Camera Z",
                         "This will overwrite the current camera Z position and therefore change the camera-to-subject "
                         "distance and the subject scale. You will need to recalibrate the Units per Pixel!\n\nAre you sure?",
                         [this, cameraId, z] {
                             if (!m_setup) return;
                             m_setup->change("Camera Z", [&](JPCellConfig& cell) {
                                 for (JPCameraConfig& c : cell.cameras)
                                     if (c.id == cameraId) {
                                         JLOGC(JPlacerLog::kCamera, JLogLevel::Info) << "Setting camera " << c.name << " Z to " << z
                                                                                    << " (previously " << c.mount.offsetZ << ")";
                                         c.mount.offsetZ = z;
                                         c.lastFocusDistanceMm.reset();
                                     }
                             });
                             m_setup->remakeForm();
                         },
                         nullptr, opts);
    } else if ((action == "captureSlotEmpty" || action == "captureSlotOccupied" || action == "resetSlotEmpty"
                || action == "resetSlotOccupied" || action == "testSlotVision")
               && path.rfind("nozzletip:", 0) == 0) {
        slotVisionAction(path.substr(10), action);
    } else if (action == "referenceTouchZ" && path.rfind("nozzletip:", 0) == 0) {
        referenceAllTouchLocationsZ();
    } else if ((action == "calibrateZ" || action == "resetZCalibration") && path.rfind("nozzletip:", 0) == 0) {
        // On the nozzle the tip is on.
        const std::string tipId = path.substr(10);
        const JPNozzleConfig* on = nullptr;
        for (const JPNozzleConfig& n : m_cell->config().nozzles)
            if (n.tipId == tipId) on = &n;
        if (!on) {
            m_window.showStatus("Load the tip on a nozzle first: it is calibrated on that nozzle", kErrorMs);
            return;
        }
        if (action == "calibrateZ" && !readyToMove()) return;
        m_cell->calibrateZ(on->id, action == "resetZCalibration");
    } else if (action == "showBackgroundProblems" && path.rfind("nozzletip:", 0) == 0) {
        // OpenPnP's Show Problems: each picture with problems beside the same with them marked, on the camera looking up.
        const auto found = m_backgroundProblems.find(path.substr(10));
        if (found == m_backgroundProblems.end() || found->second.empty()) {
            m_window.showStatus("No background problems to show: calibrate the tip's runout with background calibration on",
                                kErrorMs);
            return;
        }
        std::vector<cv::Mat> rows;
        for (size_t i = 0; i + 1 < found->second.size(); i += 2) {
            cv::Mat row;
            cv::hconcat(found->second[i], found->second[i + 1], row);
            rows.push_back(row);
        }
        cv::Mat all, rgba;
        cv::vconcat(rows, all);
        cv::cvtColor(all, rgba, cv::COLOR_BGR2RGBA);
        JPFrame shown;
        shown.width = rgba.cols;
        shown.height = rgba.rows;
        shown.rgba.assign(rgba.data, rgba.data + rgba.total() * 4);
        if (JPCameraView* view = cameraViewOf(upCameraFeed()))
            view->showPicture(shown, "Background problems: as seen, and the problems marked", kProblemsMs);
    } else if (action == "calibrateBacklash" && path.rfind("axis:", 0) == 0) {
        calibrateBacklash(path.substr(5), nullptr);
    } else if (action == "homeNozzleZ" && path.rfind("nozzle:", 0) == 0) {
        homeNozzle(path.substr(7));
    } else if (action.rfind("whiteBalance", 0) == 0 && path.rfind("camera:", 0) == 0) {
        // Worked out from what the camera sees now, as it took it; a step to undo.
        const std::string id = path.substr(7);
        JPCameraConfig::WhiteBalance wb;
        if (action != "whiteBalanceReset") {
            JPFrame frame;
            bool got = false;
            for (CameraDock& c : m_cameras)
                if (c.panel->camera().id == id) got = c.panel->feed().latestUnbalanced(frame);
            if (!got) {
                m_window.showStatus("White balance: the camera has no picture; show it first", kErrorMs);
                return;
            }
            std::string why;
            const auto v = action == "whiteBalanceMappedRoughly" ? JPWhiteBalance::automaticMapped(frame, kMappedRoughlyLevels, why)
                         : action == "whiteBalanceMappedFinely"  ? JPWhiteBalance::automaticMapped(frame, kMappedFinelyLevels, why)
                                                                 : JPWhiteBalance::automatic(frame, action == "whiteBalanceOverall", why);
            if (!v) {
                m_window.showStatus("White balance: " + why, kErrorMs);
                return;
            }
            wb = *v;
        }
        m_setup->change(action == "whiteBalanceReset" ? "White balance reset" : "White balance", [&](JPCellConfig& cell) {
            for (JPCameraConfig& cam : cell.cameras)
                if (cam.id == id) cam.whiteBalance = wb;
        });
        m_setup->remakeForm();   // its curve
    }
}

void JPlacerMachine::nozzleOffsetWizard(const std::string& nozzleId, bool storeMark) {
    if (!m_cell || !m_cell->isConnected()) {
        m_window.showStatus("Offset Wizard: connect the machine first", kErrorMs);
        return;
    }
    const auto p = m_cell->positions();
    // Where a tool is now: its axes plus its offset on the head.
    auto where = [&p](const JPMountConfig& m, double& x, double& y) {
        const auto px = p.find(m.axisX), py = p.find(m.axisY);
        if (px == p.end() || py == p.end()) return false;
        x = px->second + m.offsetX;
        y = py->second + m.offsetY;
        return true;
    };
    const JPNozzleConfig* nozzle = nullptr;
    for (const JPNozzleConfig& n : m_cell->config().nozzles) if (n.id == nozzleId) nozzle = &n;
    if (!nozzle) return;
    if (storeMark) {
        double x = 0, y = 0;
        if (!where(nozzle->mount, x, y)) {
            m_window.showStatus("Offset Wizard: " + nozzle->name + " has no X and Y position", kErrorMs);
            return;
        }
        m_nozzleMark = { nozzleId, x, y };
        m_window.showStatus("Offset Wizard: " + nozzle->name + " mark stored at X " + JPUiParts::coordinate(x) + ", Y "
                            + JPUiParts::coordinate(y) + "; now move the camera over the mark", kErrorMs);
        return;
    }
    if (!m_nozzleMark || m_nozzleMark->nozzleId != nozzleId) {
        m_window.showStatus("Offset Wizard: store the nozzle mark position first", kErrorMs);
        return;
    }
    const JPMountConfig* camera = toolMount(JPSetupForm::Tool::Camera);
    double cx = 0, cy = 0;
    if (!camera || !where(*camera, cx, cy)) {
        m_window.showStatus("Offset Wizard: no camera on a head to look at the mark with", kErrorMs);
        return;
    }
    // The mark is where the camera is; the nozzle thought it was at the stored place.
    const double dx = cx - m_nozzleMark->x, dy = cy - m_nozzleMark->y;
    m_setup->change("Offset of " + nozzle->name, [&](JPCellConfig& cell) {
        for (JPNozzleConfig& n : cell.nozzles)
            if (n.id == nozzleId) {
                n.mount.offsetX += dx;
                n.mount.offsetY += dy;
            }
    });
    m_window.showStatus("Offset Wizard: " + nozzle->name + "'s offset moved by X " + JPUiParts::coordinate(dx) + ", Y "
                        + JPUiParts::coordinate(dy), kErrorMs);
    m_nozzleMark.reset();
}

JPCameraFeed* JPlacerMachine::headCameraFeed() const {
    JPCameraPanel* p = m_cameraTasks ? m_cameraTasks->headCamera() : nullptr;
    return p ? &p->feed() : nullptr;
}

JPCameraFeed* JPlacerMachine::cameraFeed(const std::string& idOrName) const {
    for (const CameraDock& d : m_cameras)
        if (d.panel->camera().id == idOrName || d.panel->camera().name == idOrName) return &d.panel->feed();
    return nullptr;
}

JPCameraFeed* JPlacerMachine::upCameraFeed() const {
    JPCameraFeed* first = nullptr;
    for (const CameraDock& d : m_cameras) {
        const JPCameraConfig& c = d.panel->camera();
        if (!c.mount.headId.empty()) continue;
        if (m_cell && !m_cell->cameraCalibrations(c.id).empty()) return &d.panel->feed();
        if (!first) first = &d.panel->feed();
    }
    return first;
}

void JPlacerMachine::showCamera(const std::string& cameraId) {
    for (CameraDock& d : m_cameras)
        if (d.panel->camera().id == cameraId) {
            bringForward(*d.panel);
            d.panel->keepRunning(kTaskCameraMs);   // its pictures needed, shown or not
        }
}

std::string JPlacerMachine::chosenNozzleId() const {
    const JPMountConfig* m = toolMount(JPSetupForm::Tool::Nozzle);
    if (!m || !m_cell) return {};
    for (const JPNozzleConfig& n : m_cell->config().nozzles)
        if (&n.mount == m) return n.id;
    return {};
}

std::string JPlacerMachine::tipChangeRefusal(const std::string& nozzleId, const std::string& tipId) const {
    if (!m_tipChanges) return "no machine is open";
    if (m_tipChanges->busy()) return "a nozzle tip change is under way";
    return m_tipChanges->refusal(nozzleId, tipId);
}

void JPlacerMachine::keepRunout(const std::string& tipId, const std::string& nozzleId, const std::optional<JPRunout>& r) {
    if (!m_setup || !m_cell) return;
    std::string nozzleName = nozzleId;
    for (const JPNozzleConfig& n : m_cell->config().nozzles)
        if (n.id == nozzleId) nozzleName = n.name;
    m_setup->change((r ? "Runout on " : "Runout reset on ") + nozzleName, [&](JPCellConfig& cell) {
        for (JPNozzleTipConfig& t : cell.nozzleTips)
            if (t.id == tipId) {
                if (r) t.runout[nozzleId] = *r;
                else t.runout.erase(nozzleId);
            }
    });
    m_setup->remakeForm();
}

void JPlacerMachine::keepBackground(const std::string& tipId, const JPBackgroundCalibration::Result& b) {
    if (!m_setup) return;
    // The background's range and what it says, kept with the tip; its problem pictures for Show Problems.
    m_backgroundProblems[tipId] = b.problems;
    m_setup->change("Background calibration", [&](JPCellConfig& cell) {
        for (JPNozzleTipConfig& t : cell.nozzleTips)
            if (t.id == tipId) {
                JPNozzleTipConfig::Background& g = t.background;
                g.minHue = b.minHue;
                g.maxHue = b.maxHue;
                g.minSaturation = b.minSaturation;
                g.maxSaturation = b.maxSaturation;
                g.minValue = b.minValue;
                g.maxValue = b.maxValue;
                g.diagnostics = b.diagnostics;
            }
    });
    m_setup->remakeForm();
}

void JPlacerMachine::calibrateTipRunout(const std::string& nozzleId, bool ask, std::function<void(bool, const std::string&)> done) {
    if (!m_cameraTasks || !m_cell) {
        if (done) done(false, "no machine is open");
        return;
    }
    std::string tipId;
    for (const JPNozzleConfig& n : m_cell->config().nozzles)
        if (n.id == nozzleId) tipId = n.tipId;
    m_cameraTasks->calibrateRunout(nozzleId, ask, [this, tipId, nozzleId, done](bool ok, const JPRunout& r,
                                                                               const std::optional<JPBackgroundCalibration::Result>& b,
                                                                               const std::string& why) {
        if (ok) {
            keepRunout(tipId, nozzleId, r);
            if (b) keepBackground(tipId, *b);
        }
        if (done) done(ok, why);
    });
}

void JPlacerMachine::recalibrateAfterHoming(std::vector<std::string> nozzles, std::function<void(bool)> done) {
    // OpenPnP's ReferenceNozzle.home, nozzle by nozzle: the mounted tip measured again when its Auto
    // Recalibration says (with Fail Homing, failing fails the homing), the others' runout on it forgotten.
    if (nozzles.empty() || !m_cell) {
        done(true);
        return;
    }
    const std::string nozzleId = nozzles.front();
    nozzles.erase(nozzles.begin());
    const JPNozzleConfig* nozzle = nullptr;
    for (const JPNozzleConfig& n : m_cell->config().nozzles)
        if (n.id == nozzleId) nozzle = &n;
    const JPNozzleTipConfig* mounted = nullptr;
    if (nozzle)
        for (const JPNozzleTipConfig& t : m_cell->config().nozzleTips) {
            const auto& rc = t.runoutCalibration;
            if (!nozzle->fits(t.id) || (rc.recalibration != "NozzleTipChange" && rc.recalibration != "MachineHome")) continue;
            if (t.id == nozzle->tipId) mounted = &t;
            else if (t.runout.count(nozzleId)) keepRunout(t.id, nozzleId, std::nullopt);
        }
    if (!mounted || !mounted->runoutCalibration.enabled) {
        recalibrateAfterHoming(std::move(nozzles), std::move(done));
        return;
    }
    const bool failHoming = mounted->runoutCalibration.failHoming;
    calibrateTipRunout(nozzleId, false, [this, nozzles, done, failHoming](bool ok, const std::string& why) mutable {
        if (!ok && failHoming) {
            m_window.showStatus("Homing failed: " + why, kErrorMs);
            if (m_cell) m_cell->unhome();
            done(false);
            return;
        }
        recalibrateAfterHoming(std::move(nozzles), std::move(done));
    });
}

void JPlacerMachine::setTipOn(const std::string& nozzleId, const std::string& tipId) {
    if (!m_setup || !m_cell) return;
    std::string nozzle = nozzleId, tip = "no tip";
    for (const JPNozzleConfig& n : m_cell->config().nozzles) if (n.id == nozzleId) nozzle = n.name;
    for (const JPNozzleTipConfig& t : m_cell->config().nozzleTips) if (t.id == tipId) tip = t.name;
    JLOGC(JPlacerLog::kCell, JLogLevel::Info) << nozzle << ": " << tip << " on it";
    m_setup->change("Tip on " + nozzle + ": " + tip, [&](JPCellConfig& cell) {
        for (JPNozzleConfig& n : cell.nozzles)
            if (n.id == nozzleId) {
                if (!tipId.empty() && !n.fits(tipId)) n.tipIds.push_back(tipId);
                n.tipId = tipId;
            }
        // A tip is on one nozzle at a time.
        if (!tipId.empty())
            for (JPNozzleConfig& n : cell.nozzles)
                if (n.id != nozzleId && n.tipId == tipId) n.tipId.clear();
    });
    // OpenPnP's loadNozzleTip: the tip's runout forgotten when its Auto Recalibration says it is
    // measured on each load, and measured again now where it says so (or not yet measured, MachineHome).
    for (const JPNozzleTipConfig& t : m_cell->config().nozzleTips) {
        if (t.id != tipId) continue;
        const auto& rc = t.runoutCalibration;
        if ((rc.recalibration == "NozzleTipChange" || rc.recalibration == "NozzleTipChangeInJob") && t.runout.count(nozzleId))
            keepRunout(tipId, nozzleId, std::nullopt);
        bool measured = false;
        for (const JPNozzleTipConfig& now : m_cell->config().nozzleTips)
            if (now.id == tipId) measured = now.runout.count(nozzleId) > 0;
        if (rc.enabled && m_cell->isHomed() && !measured
            && (rc.recalibration == "NozzleTipChange" || rc.recalibration == "MachineHome"))
            calibrateTipRunout(nozzleId, false, nullptr);
    }
}

void JPlacerMachine::showSetupNode(const std::string& path) {
    if (m_setup && !path.empty()) m_setup->showNode(path);
}

void JPlacerMachine::changeSetup(const std::string& what, const std::function<void(JPCellConfig&)>& edit) {
    if (m_setup) m_setup->change(what, edit);
}

void JPlacerMachine::calibrateCamera(const std::string& cameraId, std::function<void(bool ok)> finished) {
    for (CameraDock& c : m_cameras)
        if (c.panel->camera().id == cameraId && m_cameraTasks) {
            m_cameraTasks->calibrate(*c.panel, std::move(finished));
            return;
        }
    if (finished) finished(false);
}

void JPlacerMachine::calibrateBacklash(const std::string& axisId, std::function<void(bool ok)> finished) {
    if (!m_cameraTasks) {
        if (finished) finished(false);
        return;
    }
    // What it found is in use already; kept through Machine Setup, a step to undo.
    m_cameraTasks->calibrateBacklash(axisId, [this, axisId](const JPBacklashCalibrator::Result& r) {
        if (!m_setup) return;
        std::string name = axisId;
        if (const JPAxisConfig* a = m_cell->config().axis(axisId)) name = a->name;
        m_setup->change("Backlash of " + name, [&](JPCellConfig& cell) {
            for (JPAxisConfig& a : cell.axes)
                if (a.id == axisId) {
                    a.backlash = r.method;
                    a.backlashOffset = r.offset;
                    a.sneakUpMm = r.sneakUpMm;
                    a.backlashSpeedFactor = r.speedFactor;
                    a.backlashTable = r.table;
                    a.approachMm = r.approachMm;
                    a.backlashCalibration = r.data;
                }
        });
        m_setup->remakeForm();
    }, std::move(finished));
}

void JPlacerMachine::calibrateTip(const std::string& tipId, std::function<void(bool ok)> finished) {
    const JPNozzleConfig* on = nullptr;
    if (m_cell)
        for (const JPNozzleConfig& n : m_cell->config().nozzles)
            if (n.tipId == tipId) on = &n;
    if (!on) {
        m_window.showStatus("Load the tip on a nozzle first: it is calibrated on that nozzle", kErrorMs);
        if (finished) finished(false);
        return;
    }
    calibrateTipRunout(on->id, true, [finished](bool ok, const std::string&) {
        if (finished) finished(ok);
    });
}

void JPlacerMachine::enableVisualHoming(const std::string& headId, std::function<void(bool ok)> finished) {
    if (!m_cameraTasks) {
        if (finished) finished(false);
        return;
    }
    m_cameraTasks->captureMark(headId, [this, headId, finished](std::optional<JPlacerCameraTasks::Mark> mark) {
        if (mark)
            changeSetup("Visual homing", [&](JPCellConfig& cell) {
                for (JPHeadConfig& h : cell.heads)
                    if (h.id == headId) {
                        const double z = h.homingFiducial ? h.homingFiducial->z : 0.0;
                        h.homingFiducial = JPMachineLocation { mark->x, mark->y, z, 0 };
                        h.homingFiducialDiameter = mark->diameter;
                        h.visualHoming = true;
                    }
            });
        if (finished) finished(mark.has_value());
    });
}

bool JPlacerMachine::onMainWait(const std::function<void()>& fn) {
    if (std::this_thread::get_id() == m_mainThread) {
        fn();
        return true;
    }
    std::weak_ptr<bool> alive = m_alive;
    auto done = std::make_shared<std::promise<void>>();
    auto result = done->get_future();
    JMainThreadDispatcher::instance().post([alive, fn, done] {
        if (const auto a = alive.lock(); a && *a) fn();
        done->set_value();
    });
    while (result.wait_for(std::chrono::milliseconds(kScriptPollMs)) != std::future_status::ready)
        if (const auto a = alive.lock(); !a || !*a) return false;
    return true;
}

JJson JPlacerMachine::scriptRequest(const JJson& request) {
    JJson answer = JJson::object();
    auto fail = [&answer](const std::string& why) {
        answer["error"] = why;
        return answer;
    };
    if (!m_cell) return fail("no machine is open");
    const std::string call = request["call"].str();
    const JPCellConfig& cfg = m_cell->config();
    // Waiting for the cell from its own thread would never end (an actuator's script, a homing event's).
    const bool moves = call == "moveTo" || call == "safeZ" || call == "home" || call == "actuate" || call == "read"
                    || call == "pick" || call == "place" || call == "readQrCode" || call == "pipeline";
    if (moves && m_cell->onCellThread())
        return fail(call + " cannot be asked from a script the machine itself is running (an actuator's, or a homing event's)");
    // A tool by its name or id: a nozzle, a camera or an actuator on a head.
    auto tool = [&cfg](const std::string& name) -> const JPMountConfig* {
        for (const JPNozzleConfig& n : cfg.nozzles) if (n.name == name || n.id == name) return &n.mount;
        for (const JPCameraConfig& c : cfg.cameras) if (c.name == name || c.id == name) return &c.mount;
        for (const JPActuatorConfig& a : cfg.actuators) if (a.name == name || a.id == name) return &a.mount;
        return nullptr;
    };
    auto actuator = [&cfg](const std::string& name) -> const JPActuatorConfig* {
        for (const JPActuatorConfig& a : cfg.actuators) if (a.name == name || a.id == name) return &a;
        return nullptr;
    };
    auto optional = [&request](const char* key) {
        const JJson& v = request[key];
        return v.isNumber() ? std::optional<double>(v.number()) : std::nullopt;
    };
    const double speed = request["speed"].isNumber() ? std::clamp(request["speed"].number(), 0.01, 1.0) : 1.0;
    std::string why;
    if (call == "positions") {
        JJson result = JJson::object();
        for (const auto& [id, v] : m_cell->positions()) {
            const JPAxisConfig* a = cfg.axis(id);
            result[a ? a->name : id] = v;
        }
        answer["result"] = result;
    } else if (call == "location") {
        const JPMountConfig* m = tool(request["tool"].str());
        if (!m) return fail("no nozzle, camera or actuator " + request["tool"].str());
        const auto p = m_cell->positions();
        auto at = [&p](const std::string& axis, double offset) -> JJson {
            const auto i = p.find(axis);
            return axis.empty() || i == p.end() ? JJson() : JJson(i->second + offset);
        };
        JJson result = JJson::object();
        result["x"] = at(m->axisX, m->offsetX);
        result["y"] = at(m->axisY, m->offsetY);
        result["z"] = at(m->axisZ, m->offsetZ);
        result["rotation"] = at(m->axisRotation, 0);
        answer["result"] = result;
    } else if (call == "moveTo") {
        const JPMountConfig* m = tool(request["tool"].str());
        if (!m) return fail("no nozzle, camera or actuator " + request["tool"].str());
        const std::array<std::optional<double>, 4> to { optional("x"), optional("y"), optional("z"), optional("rotation") };
        const JPMountConfig mount = *m;
        const bool ok = request["straight"].boolean() ? m_cell->moveToolStraightAndWait(mount, to, speed, why)
                                                      : m_cell->moveToolAndWait(mount, to, speed, why);
        if (!ok) return fail(why);
    } else if (call == "safeZ") {
        std::string head = cfg.heads.empty() ? std::string() : cfg.heads.front().id;
        for (const JPHeadConfig& h : cfg.heads) if (h.name == request["head"].str() || h.id == request["head"].str()) head = h.id;
        if (head.empty()) return fail("no head");
        if (!m_cell->safeZAndWait(head, speed, why)) return fail(why);
    } else if (call == "home") {
        m_cell->home();
        // Until it is homed, or stops trying.
        const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(kScriptHomeMs);
        std::this_thread::sleep_for(std::chrono::milliseconds(kScriptPollMs));
        while (m_cell->isHoming() && std::chrono::steady_clock::now() < until)
            std::this_thread::sleep_for(std::chrono::milliseconds(kScriptPollMs));
        if (!m_cell->isHomed()) return fail("the machine did not home");
    } else if (call == "actuate") {
        const JPActuatorConfig* a = actuator(request["actuator"].str());
        if (!a) return fail("no actuator " + request["actuator"].str());
        const JJson& v = request["value"];
        const bool ok = v.isBool() ? m_cell->switchActuatorAndWait(a->id, v.boolean(), why)
                                   : m_cell->setActuatorAndWait(a->id, v.isNumber() ? JPXmlValues::number(v.number()) : v.str(), why);
        if (!ok) return fail(why);
    } else if (call == "read") {
        const JPActuatorConfig* a = actuator(request["actuator"].str());
        if (!a) return fail("no actuator " + request["actuator"].str());
        std::string value;
        const std::optional<std::string> parameter =
            request["parameter"].isNull() ? std::nullopt : std::optional<std::string>(request["parameter"].str());
        if (!m_cell->readActuatorAndWait(a->id, parameter, value, why)) return fail(why);
        answer["result"] = value;
    } else if (call == "gcode") {
        std::string driver = cfg.drivers.empty() ? std::string() : cfg.drivers.front().id;
        for (const JPDriverConfig& d : cfg.drivers) if (d.name == request["controller"].str() || d.id == request["controller"].str()) driver = d.id;
        if (driver.empty()) return fail("no controller");
        m_cell->sendLine(driver, request["line"].str());
    } else if (call == "machine") {
        // What OpenPnP's scripting objects are made from: the heads with their
        // nozzles (and the part each holds), cameras and actuators; the
        // machine's own cameras and actuators; the feeders; the parts.
        auto named = [](const std::string& id, const std::string& name) {
            JJson o = JJson::object();
            o["id"] = id;
            o["name"] = name.empty() ? id : name;
            return o;
        };
        std::map<std::string, std::string> holding;
        JJson feeders = JJson::array(), parts = JJson::array();
        if (!onMainWait([&] {
                holding = m_nozzleParts;
                if (!m_configuration) return;
                for (const JPFeeder& f : m_configuration->feeders()) {
                    JJson o = named(f.id(), f.name());
                    o["part"] = f.partId();
                    o["enabled"] = f.enabled();
                    o["feedCount"] = f.number("feed-count");
                    feeders.push(o);
                }
                for (const auto& p : m_configuration->parts()) {
                    JJson o = named(p->id, p->name.value_or(std::string()));
                    o["package"] = p->packageId;
                    o["height"] = p->height.convertToUnits(JPLengthUnit::Millimeters).value();
                    parts.push(o);
                }
            }))
            return fail("jplacer is closing");
        auto cameraOf = [&](const JPCameraConfig& c) {
            JJson o = named(c.id, c.name);
            o["looking"] = c.looksUp ? "Up" : "Down";
            return o;
        };
        JJson heads = JJson::array(), cameras = JJson::array(), actuators = JJson::array();
        for (const JPHeadConfig& h : cfg.heads) {
            JJson o = named(h.id, h.name);
            o["nozzles"] = JJson::array();
            o["cameras"] = JJson::array();
            o["actuators"] = JJson::array();
            for (const JPNozzleConfig& n : cfg.nozzles) {
                if (n.mount.headId != h.id) continue;
                JJson z = named(n.id, n.name);
                z["tip"] = n.tipId;
                const auto held = holding.find(n.id);
                z["part"] = held == holding.end() ? std::string() : held->second;
                o["nozzles"].push(z);
            }
            for (const JPCameraConfig& c : cfg.cameras) if (c.mount.headId == h.id) o["cameras"].push(cameraOf(c));
            for (const JPActuatorConfig& a : cfg.actuators) if (a.mount.headId == h.id) o["actuators"].push(named(a.id, a.name));
            heads.push(o);
        }
        for (const JPCameraConfig& c : cfg.cameras) if (c.mount.headId.empty()) cameras.push(cameraOf(c));
        for (const JPActuatorConfig& a : cfg.actuators) if (a.mount.headId.empty()) actuators.push(named(a.id, a.name));
        JJson result = JJson::object();
        result["name"] = cfg.name;
        result["heads"] = heads;
        result["cameras"] = cameras;
        result["actuators"] = actuators;
        result["feeders"] = feeders;
        result["parts"] = parts;
        answer["result"] = result;
    } else if (call == "pick" || call == "place") {
        // OpenPnP's Nozzle.pick(part) and place(), where the nozzle is.
        const JPNozzleConfig* n = nullptr;
        for (const JPNozzleConfig& z : cfg.nozzles)
            if (z.name == request["nozzle"].str() || z.id == request["nozzle"].str()) n = &z;
        if (!n) return fail("no nozzle " + request["nozzle"].str());
        const std::string id = n->id, part = request["part"].str();
        if (call == "pick") {
            if (!part.empty() && !onMainWait([&] { setNozzlePart(id, part); })) return fail("jplacer is closing");
            if (!m_cell->pickAndWait(id, why)) {
                onMainWait([&] { setNozzlePart(id, ""); });
                return fail(why);
            }
        } else {
            if (!m_cell->placeAtAndWait(id, { std::nullopt, std::nullopt, std::nullopt, std::nullopt }, speed, why)) return fail(why);
            onMainWait([&] { setNozzlePart(id, ""); });
        }
    } else if (call == "setFeedCount") {
        bool found = false;
        if (!onMainWait([&] {
                if (!m_configuration) return;
                for (const JPFeeder& f : m_configuration->feeders())
                    if (f.name() == request["feeder"].str() || f.id() == request["feeder"].str())
                        if (JPFeeder* g = m_configuration->feeder(f.id())) {
                            g->setNumber("feed-count", request["count"].number());
                            found = true;
                        }
                if (found && onSetupConfigurationChanged) onSetupConfigurationChanged();
            }))
            return fail("jplacer is closing");
        if (!found) return fail("no feeder " + request["feeder"].str());
    } else if (call == "job" || call == "setBoardEnabled" || call == "boardPlacementLocation" || call == "refreshJob") {
        if (!onScriptJobRequest) return fail("no job");
        if (!onMainWait([&] { answer = onScriptJobRequest(request); })) return fail("jplacer is closing");
    } else if (call == "readQrCode") {
        // OpenPnP's VisionUtils.readQrCode: what a QR code under the head camera says, where it is now.
        JPlacerJobMachine* jm = scriptJobMachine ? scriptJobMachine() : nullptr;
        if (!jm) return fail("no camera to read with");
        const auto at = jm->cameraLocation();
        if (!at) return fail("where the camera is is not known");
        std::vector<JPJobMachine::QrCode> codes;
        if (!jm->readQrCodes(*at, codes, why)) return fail(why);
        answer["result"] = codes.empty() ? JJson() : JJson(codes.front().text);
    } else if (call == "pipeline" || call == "showPipelineImage") {
        // OpenPnP's CvPipeline.process and showFilteredImage, on the head camera where it is.
        JPlacerJobMachine* jm = scriptJobMachine ? scriptJobMachine() : nullptr;
        if (!jm) return fail("no camera to look with");
        JJson result;
        const bool ok = call == "pipeline" ? m_scriptVision.run(*jm, request, result, why)
                                           : m_scriptVision.show(*jm, int(request["ms"].number(kErrorMs)), request["text"].str(), why);
        if (!ok) return fail(why);
        answer["result"] = result;
    } else if (call == "dialog") {
        // OpenPnP's JOptionPane.showMessageDialog, shown without waiting.
        std::weak_ptr<bool> alive = m_alive;
        JMainThreadDispatcher::instance().post([alive, title = request["title"].str(), text = request["text"].str()] {
            if (const auto a = alive.lock(); a && *a) JDialog::message(title.empty() ? "Message" : title, text);
        });
    } else if (call == "message") {
        std::weak_ptr<bool> alive = m_alive;
        JMainThreadDispatcher::instance().post([this, alive, text = request["text"].str()] {
            if (const auto a = alive.lock(); a && *a) m_window.showStatus(text, kErrorMs);
        });
    } else {
        return fail("no such call: " + call);
    }
    return answer;
}

JJson JPlacerMachine::cameraDeviceControls(const std::string& cameraId) const {
    for (const CameraDock& d : m_cameras)
        if (d.panel->camera().id == cameraId) return d.panel->feed().deviceControls();
    return JJson::object();
}

bool JPlacerMachine::cameraRenderingSmooth(const std::string& cameraId) const {
    for (const CameraDock& d : m_cameras)
        if (d.panel->camera().id == cameraId) return d.panel->view().renderingQuality() != JPCameraView::RenderingQuality::Low;
    return true;   // no picture to draw
}

void JPlacerMachine::setCameraRenderingSmooth(const std::string& cameraId, bool smooth) {
    using Q = JPCameraView::RenderingQuality;
    for (CameraDock& d : m_cameras)
        if (d.panel->camera().id == cameraId) {
            JPCameraView& view = d.panel->view();
            view.setRenderingQuality(smooth ? Q::High : Q::Low);
            if (view.onRenderingQualityChanged) view.onRenderingQualityChanged(view.renderingQuality());
        }
}

bool JPlacerMachine::cameraCalibrated(const std::string& cameraId) const {
    return m_cell && !m_cell->cameraCalibrations(cameraId).empty();
}

void JPlacerMachine::ensurePhotonActuator() {
    if (!m_setup || !m_cell || m_cell->config().actuatorNamed(JPPhotonBus::kDataActuator)) return;
    JLOGC(JPlacerLog::kCell, JLogLevel::Info) << "Photon feeders: actuator " << JPPhotonBus::kDataActuator << " made";
    m_setup->change(std::string("Actuator ") + JPPhotonBus::kDataActuator, [](JPCellConfig& cell) {
        JPActuatorConfig a;
        a.id = JPSetupEdits::newId(cell, "ACT");
        a.name = JPPhotonBus::kDataActuator;
        a.valueType = JPActuatorConfig::ValueType::Text;
        if (!cell.drivers.empty()) a.driverId = cell.drivers.front().id;
        a.readCommand = "M485 {value}";
        a.readPattern = "rs485-reply: (.*)";
        cell.actuators.push_back(a);
    });
}

void JPlacerMachine::setEditItems(JMenuItem* undo, JMenuItem* redo) {
    m_undoItem = undo;
    m_redoItem = redo;
    updateEditItems();
}

void JPlacerMachine::updateEditItems() {
    if (m_undoItem) {
        m_undoItem->setEnabled(m_setup && m_setup->canUndo());
        m_undoItem->setLabel(m_setup ? m_setup->undoLabel() : "Undo");
    }
    if (m_redoItem) {
        m_redoItem->setEnabled(m_setup && m_setup->canRedo());
        m_redoItem->setLabel(m_setup ? m_setup->redoLabel() : "Redo");
    }
}

void JPlacerMachine::jogAction(const std::string& action) {
    // Stopping works with the Jog panel closed too.
    if (action == "stop" || action == "emergencyStop") stop(action == "emergencyStop");
    else if (m_jog) m_jog->act(action);
}

std::vector<double> JPlacerMachine::jogDistances() {
    std::string why;
    const std::string kept = JSettings::instance().get<std::string>(JPlacerSettings::jogDistancesKey(), "");
    std::vector<double> steps = JPJogPanel::parseSteps(kept, kLeastJogDistance, kMostJogDistance, why);
    return steps.empty() ? JPJogPanel::defaultDistances() : steps;
}

std::vector<double> JPlacerMachine::jogSpeeds() {
    std::string why;
    const std::string kept = JSettings::instance().get<std::string>(JPlacerSettings::kJogSpeeds, "");
    std::vector<double> steps = JPJogPanel::parseSteps(kept, 1, 100, why);   // kept in %
    if (steps.empty()) return JPJogPanel::defaultSpeeds();
    for (double& v : steps) v /= 100;
    return steps;
}

void JPlacerMachine::jogStepsChanged() {
    if (m_jog) m_jog->setSteps(jogDistances(), jogSpeeds());
}

void JPlacerMachine::keysChanged() {
    if (m_jog) m_jog->refreshKeys();
}

void JPlacerMachine::undo() {
    if (m_setup) m_setup->undo();
}

void JPlacerMachine::redo() {
    if (m_setup) m_setup->redo();
}

void JPlacerMachine::showState() {
    using S = JPStateIcon::State;
    const bool open = m_cell != nullptr, connected = open && m_cell->isConnected();

    m_connectIcon.setEnabled(open);
    m_connectIcon.setState(m_connecting ? S::Busy : connected ? S::Good
                           : (m_connectFailed || !m_lost.empty()) ? S::Fault : S::Idle);
    m_connectIcon.setTooltip(!open        ? "No machine open"
                             : connected  ? m_cell->config().name + ": connected. Click to disconnect."
                             : m_connecting ? "Connecting\xE2\x80\xA6"
                                            : m_cell->config().name + ": not connected. Click to connect.");

    const bool homing = connected && m_cell->isHoming();
    m_homeIcon.setEnabled(connected && !homing);
    m_homeIcon.setState(homing ? S::Busy : (connected && m_cell->isHomed()) ? S::Good
                        : (connected && m_homeFailed) ? S::Fault : S::Idle);
    m_homeIcon.setTooltip(!connected ? "Connect to home the machine"
                          : homing   ? "Homing\xE2\x80\xA6"
                          : m_cell->isHomed() ? "Homed. Click to home again."
                                              : "Not homed: the machine will not move until it is. Click to home.");

    // The strip: only what must not be missed.
    if (connected && m_cell->inAlarm())
        m_window.setNotice("ALARM", "A controller has stopped on an alarm (a limit switch, an emergency stop, "
                           "or a failed home). Find the cause; the Console shows what it said.", Colors::Danger);
    else if (!connected && !m_lost.empty())
        m_window.setNotice("CONNECTION LOST", m_lost, Colors::Danger);
    else
        m_window.setNotice("");
}

void JPlacerMachine::chooseCell() {
    JDialog::openFile("Open Cell", { "json" }, [this](std::string path) {
        std::string error;
        if (!openCell(path, error)) JDialog::message("The cell could not be opened", error);
    });
}

void JPlacerMachine::importOpenPnp() {
    // OpenPnP keeps its configuration in .openpnp2 in the home folder: offer
    // that file first, rather than sending the person to find a hidden folder.
    const char* home = std::getenv(
#if defined(_WIN32)
        "USERPROFILE"
#else
        "HOME"
#endif
    );
    const std::string usual = home ? (std::filesystem::path(home) / kOpenPnpDir / kOpenPnpMachineFile).string()
                                   : std::string();
    auto choose = [this] {
        JDialog::openFile("Import OpenPnP Machine (machine.xml)", { "xml" },
                          [this](std::string path) { importFrom(path); });
    };
    std::error_code ec;
    if (usual.empty() || !std::filesystem::exists(usual, ec)) {
        choose();
        return;
    }
    JDialogOptions opts;
    opts.okLabel     = "Import";
    opts.cancelLabel = "Choose Another File\xE2\x80\xA6";
    JDialog::confirm("Import OpenPnP Machine", "Import OpenPnP's machine from " + usual + "?",
                     [this, usual] { importFrom(usual); }, choose, opts);
}

void JPlacerMachine::startWithDefault() {
    if (!JPlacerSettings::machineCell().empty()) return;
    const std::string shipped = JPlacerPaths::bundled(JPOpenPnpMachineImporter::kDefaultsDir);
    if (shipped.empty()) return;
    const std::filesystem::path machine =
        std::filesystem::path(shipped) / JPOpenPnpMachineImporter::kDefaultsConfig / kOpenPnpMachineFile;
    std::error_code ec;
    if (!std::filesystem::exists(machine, ec)) return;
    JLOGC(JPlacerLog::kApp, JLogLevel::Info) << "no machine yet: OpenPnP's default machine from " << machine.string();
    importFrom(machine.string(), false);
    // OpenPnP's sample job, its board and panel, where they can be opened
    // (OpenPnP keeps its samples beside it): the configuration's samples folder.
    const std::filesystem::path samples = std::filesystem::path(shipped) / kSamplesDir;
    const std::filesystem::path to = std::filesystem::path(JPlacerPaths::configDir()) / kSamplesDir;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(samples, ec)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".xml") continue;
        const std::filesystem::path target = to / std::filesystem::relative(entry.path(), samples, ec);
        std::filesystem::create_directories(target.parent_path(), ec);
        std::filesystem::copy_file(entry.path(), target, std::filesystem::copy_options::skip_existing, ec);
    }
    if (std::filesystem::exists(to, ec)) JLOGC(JPlacerLog::kApp, JLogLevel::Info) << "OpenPnP's samples in " << to.string();
}

void JPlacerMachine::importFrom(const std::string& path, bool tell) {
    JPCellConfig cell;
    std::vector<std::string> notes;
    std::string error;
    if (!JPOpenPnpMachineImporter::import(path, cell, notes, error)) {
        JDialog::message("The OpenPnP machine could not be imported", error);
        return;
    }
    const std::string target = (std::filesystem::path(cellsDir()) / kImportedCellFile).string();
    // Importing again keeps what was set, taught or measured here.
    JPCellConfig previous;
    std::string ignored;
    std::error_code ec;
    if (std::filesystem::exists(target, ec) && previous.load(target, ignored))
        JPOpenPnpMachineImporter::keepFrom(previous, cell);
    if (!cell.save(target, error) || !openCell(target, error)) {
        JDialog::message("The OpenPnP machine could not be imported", error);
        return;
    }
    std::string body = std::to_string(cell.drivers.size()) + " controller(s), " + std::to_string(cell.axes.size())
                     + " axes, " + std::to_string(cell.nozzles.size()) + " nozzle(s), "
                     + std::to_string(cell.nozzleTips.size()) + " nozzle tip(s), "
                     + std::to_string(cell.cameras.size()) + " camera(s) and "
                     + std::to_string(cell.actuators.size()) + " actuator(s), saved as " + target + ".";
    if (!notes.empty()) {
        body += "\n\nTo check:";
        for (const std::string& n : notes) body += "\n\xE2\x80\xA2 " + n;
    }
    if (onImported) onImported(path);
    if (tell) JDialog::message("OpenPnP machine imported", body);
    else for (const std::string& n : notes) JLOGC(JPlacerLog::kApp, JLogLevel::Info) << "OpenPnP's default machine: " << n;
}

void JPlacerMachine::connect() {
    if (!m_cell || m_cell->isConnected()) return;
    m_connecting = true;
    m_connectFailed = false;
    showState();
    m_cell->connect();
}

void JPlacerMachine::disconnect() {
    if (m_cell) m_cell->disconnect();
}

void JPlacerMachine::home() {
    if (!m_cell || !m_cell->isConnected()) return;
    m_homeFailed = false;
    m_cell->home();
    showState();
}

void JPlacerMachine::showLight(const std::string& light, std::optional<bool> on) {
    if (on) m_lights[light] = *on;
    else m_lights.erase(light);
    for (CameraDock& c : m_cameras)
        if (c.panel->camera().lightActuator() == light) c.panel->view().setLight(true, on);
}

void JPlacerMachine::moveNozzleToCamera(const std::string& cameraId) {
    const JPMountConfig* nozzle = toolMount(JPSetupForm::Tool::Nozzle);
    if (!nozzle || !m_cell) {
        m_window.showStatus("No nozzle to move", kErrorMs);
        return;
    }
    if (!readyToMove()) return;
    for (const JPCameraConfig& cam : m_cell->config().cameras)
        if (cam.id == cameraId) {
            // Over the camera at its focal plane, the nozzle's rotation kept; by way of safe Z.
            m_cell->moveTool(*nozzle, { cam.mount.offsetX, cam.mount.offsetY, cam.mount.offsetZ, std::nullopt }, 1.0);
            selectMoved(*nozzle);
        }
}

void JPlacerMachine::toggleLight(const std::string& light) {
    if (!m_cell || !m_cell->isConnected()) {
        m_window.showStatus("Connect the machine first", kErrorMs);
        return;
    }
    const auto it = m_lights.find(light);
    m_cell->switchActuator(light, it == m_lights.end() || !it->second);
}

void JPlacerMachine::runEvent(const std::string& event, std::function<void()> then, JJson globals) {
    // Off the screen's thread: a script may take its time; what fails is said in the log and the status line.
    std::thread([this, event, then = std::move(then), globals = std::move(globals), scripting = m_scripting,
                 alive = std::weak_ptr<bool>(m_alive)] {
        std::string why;
        const bool ok = scripting->on(event, globals, why);
        if (!ok) JLOGC(JPlacerLog::kApp, JLogLevel::Warn) << event << ": " << why;
        if (ok && !then) return;
        JMainThreadDispatcher::instance().post([this, alive, ok, why, event, then] {
            const auto a = alive.lock();
            if (!a || !*a) return;
            if (!ok) m_window.showStatus(event + ": " + why, kErrorMs);
            if (then) then();
        });
    }).detach();
}

void JPlacerMachine::lightCameras() {
    if (!m_cell) return;
    const bool connected = m_cell->isConnected();
    // A light is on while any camera it lights runs and is set to light it
    // for you to look at (User Camera Action).
    std::map<std::string, bool> lights;
    for (CameraDock& c : m_cameras) {
        const std::string light = c.panel->camera().lightActuator();
        if (light.empty()) continue;
        lights[light] = lights[light] || (c.panel->isRunning() && c.panel->camera().light.userAction);
        if (!connected) c.panel->setNote("Light off: connect the machine to light this camera.");
    }
    if (!connected) return;
    for (const auto& [light, on] : lights) m_cell->switchActuator(light, on);
}

void JPlacerMachine::showSetup(const std::string& path) {
    if (!m_setup) return;
    showDock("Machine Setup");
    m_setup->showNode(path);
}

bool JPlacerMachine::showDock(const std::string& title) {
    for (CameraDock& c : m_cameras)
        if (c.dock->title() == title) {
            bringForward(*c.panel);
            return true;
        }
    for (Dock& d : m_docks) {
        if (d.dock->title() != title) continue;
        m_layout.show(d.dock.get());
        return true;
    }
    return false;
}

} // inline namespace jf
