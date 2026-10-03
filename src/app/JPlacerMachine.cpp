// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerMachine.h"

#include "JPlacerSettings.h"

#include "common/JPlacerLog.h"
#include "common/JPlacerPaths.h"
#include "openpnp/JPOpenPnpMachineImporter.h"
#include "ui/JPActuatorPanel.h"
#include "ui/JPAxesPanel.h"
#include "ui/JPConsolePanel.h"
#include "ui/JPJogPanel.h"
#include "ui/JPMachinePanel.h"
#include "ui/JPMachineSetupPanel.h"

#include <j/config/Settings.h>
#include <j/core/Dialog.h>
#include <j/core/Log.h>
#include <j/core/MainThreadDispatcher.h>

#include <cstdlib>
#include <filesystem>
#include <map>

inline namespace jf {

namespace {

// The file an imported OpenPnP machine is written to, in cellsDir().
constexpr const char* kImportedCellFile = "openpnp.json";
// How long a status-bar message stays: a confirmation briefly, a failure long
// enough to read the reason.
constexpr int kStatusMs = 3000;
constexpr int kErrorMs  = 8000;

// How fast Park Head moves, as a share of the axes' rates.
constexpr double kParkSpeed = 0.5;

// The first so many panels dock on the right (driving the machine), the rest
// along the bottom (what it says).
constexpr size_t kRightPanels = 5;

// Where OpenPnP keeps its machine, under the home folder.
constexpr const char* kOpenPnpDir         = ".openpnp2";
constexpr const char* kOpenPnpMachineFile = "machine.xml";

} // namespace

JPlacerMachine::JPlacerMachine(JAppWindow& window, JSceneGraph& graph)
    : m_window(window), m_graph(graph), m_profiles(JPFirmwareProfile::loadAll()),
      m_connectIcon(graph), m_homeIcon(graph) {
    // The machine's two states, always in view: click the chip to connect or
    // disconnect, the house to home.
    JToolBar& tb = window.toolBar();
    tb.addWidget(&m_connectIcon);
    tb.addWidget(&m_homeIcon);
    m_connectIcon.onClicked.connect([this] {
        if (m_cell && m_cell->isConnected()) disconnect(); else connect();
    });
    m_homeIcon.onClicked.connect([this] { home(); });
    window.dockSpace().setCentreDocks(true);   // a tab per camera (buildCameras)
    JLOGC(JPlacerLog::kProfiles, JLogLevel::Info) << m_profiles.size() << " firmware profile(s)";
    if (const std::string last = JPlacerSettings::machineCell(); !last.empty()) {
        std::string error;
        if (!openCell(last, error)) JLOGC(JPlacerLog::kApp, JLogLevel::Warn) << error;
    }
}

JPlacerMachine::~JPlacerMachine() {
    *m_alive = false;
    dropPanels();         // they stop listening to the cell before the cell goes
    m_cell.reset();
}

void JPlacerMachine::dropPanels() {
    for (CameraDock& c : m_cameras) c.panel->setMarks(nullptr);   // they come from the board, which goes first
    if (m_board) m_board->dropPanel();
    m_board.reset();
    m_cameraTasks.reset();   // a task under way finishes first: it drives the cell and a camera
    for (const auto& u : m_unwatch) u();
    m_unwatch.clear();
    for (Dock& d : m_docks) {
        d.dock->setContent(nullptr);
        d.panel.reset();
    }
    for (CameraDock& c : m_cameras)
        if (JDockHost* host = c.dock->placedIn()) host->removeDock(c.dock.get());
    m_cameras.clear();
}

void JPlacerMachine::buildCameras() {
    // The cameras fill the centre, each in a tab: what the machine sees is
    // what the person works from. A camera runs, and its light is on, while
    // its tab is in front (or torn out into a window of its own).
    // A head camera looks where its axes put it, plus its offset on the head;
    // a fixed one is drawn with nothing under it. The world it draws stays
    // where the switches put it when visual homing corrects the coordinates.
    const std::string captures = (std::filesystem::path(JPlacerPaths::configDir()) / "captures").string();
    std::vector<JPCameraPanel*> panels;
    for (const JPCameraConfig& c : m_cell->config().cameras) {
        std::function<bool(double&, double&)> view;
        if (!c.mount.axisX.empty() && !c.mount.axisY.empty())
            view = [cell = m_cell.get(), m = c.mount](double& x, double& y) {
                const auto p = cell->positions();
                const auto px = p.find(m.axisX), py = p.find(m.axisY);
                if (px == p.end() || py == p.end()) return false;
                const auto corrected = cell->correctionSinceHome();
                const auto cx = corrected.find(m.axisX), cy = corrected.find(m.axisY);
                x = px->second + m.offsetX + (cx == corrected.end() ? 0 : cx->second);
                y = py->second + m.offsetY + (cy == corrected.end() ? 0 : cy->second);
                return true;
            };
        CameraDock d;
        d.panel = std::make_unique<JPCameraPanel>(m_graph, m_window.hal(), c, captures, std::move(view),
                                                  [cell = m_cell.get()](const std::string& id, int width, int height) {
                                                      return cell->cameraCalibration(id, width, height);
                                                  });
        // Straightened or as taken, kept from last time.
        const JSettings& s = JSettings::instance();
        d.panel->setView(s.get<bool>(JPlacerSettings::cameraStraightKey(c.id), false),
                         s.get<double>(JPlacerSettings::cameraShowAllKey(c.id), 0.0));
        d.panel->onViewChanged = [id = c.id](bool straight, double showAll) {
            JSettings::instance().set(JPlacerSettings::cameraStraightKey(id), straight);
            JSettings::instance().set(JPlacerSettings::cameraShowAllKey(id), showAll);
            JPlacerSettings::save();
        };
        d.panel->onRunning = [this](bool) { lightCameras(); };
        d.dock = std::make_unique<JDockWidget>(c.name, 0.f, 0.f, 0.f, 0.f);
        d.dock->setCloseable(false);   // nowhere to open it again from
        d.dock->setContent(d.panel.get());
        panels.push_back(d.panel.get());
        m_cameras.push_back(std::move(d));
    }
    JDockHost& centre = m_window.dockSpace().host(JDockSpace::Center);
    // Tabbed together: each joins the first one's group.
    for (size_t i = 0; i < m_cameras.size(); ++i) {
        if (i == 0) centre.addDock(m_cameras[i].dock.get());
        else centre.insertDock(m_cameras[i].dock.get(), centre.findDock(m_cameras[0].dock.get()));
    }
    if (!m_cameras.empty()) bringForward(*m_cameras.front().panel);
    m_cameraTasks = std::make_unique<JPlacerCameraTasks>(m_window, *m_cell, std::move(panels),
                                                         [this](JPCameraPanel& p) { bringForward(p); }, m_cellPath);
    lightCameras();
}

void JPlacerMachine::bringForward(JPCameraPanel& camera) {
    for (CameraDock& d : m_cameras)
        if (d.panel.get() == &camera)
            // Re-inserting a dock where it already is makes it the active tab.
            if (JDockHost* host = d.dock->placedIn()) host->insertDock(d.dock.get(), host->findDock(d.dock.get()));
}

void JPlacerMachine::buildPanels() {
    buildCameras();

    auto machine = std::make_unique<JPMachinePanel>(m_graph, *m_cell);
    machine->onPortChosen = [this](const std::string& driverId, const std::string& port) {
        // Posted: the choice arrives inside the panel's own event, and
        // reopening the cell replaces that panel.
        std::weak_ptr<bool> alive = m_alive;
        JMainThreadDispatcher::instance().post([this, alive, driverId, port] {
            if (const auto a = alive.lock(); a && *a) setPort(driverId, port);
        });
    };
    std::vector<std::pair<const char*, std::unique_ptr<JContainer>>> panels;
    panels.emplace_back("Machine",   std::move(machine));
    panels.emplace_back("Jog",       std::make_unique<JPJogPanel>(m_graph, *m_cell));
    panels.emplace_back("Actuators", std::make_unique<JPActuatorPanel>(m_graph, *m_cell));
    m_board = std::make_unique<JPlacerBoard>(m_window, *m_cameraTasks,
        [this](const JPMountConfig& mount, double xPerY) { squareMachine(mount, xPerY); });
    panels.emplace_back("Board",     m_board->makePanel(m_graph));
    for (CameraDock& c : m_cameras)
        c.panel->setMarks([board = m_board.get(), id = c.panel->camera().id] { return board->marks(id); });
    std::vector<std::string> profiles;
    for (const JPFirmwareProfile& p : m_profiles) profiles.push_back(p.id());
    auto setup = std::make_unique<JPMachineSetupPanel>(m_graph, m_cell->config(), profiles, m_setupSelected);
    setup->onSelected = [this](const std::string& path) { m_setupSelected = path; };
    setup->onApply = [this](const JPCellConfig& cell) {
        // Posted: Apply arrives inside the panel's own event, and opening
        // the cell again replaces that panel.
        std::weak_ptr<bool> alive = m_alive;
        JMainThreadDispatcher::instance().post([this, alive, cell] {
            if (const auto a = alive.lock(); a && *a) applySetup(cell);
        });
    };
    panels.emplace_back("Machine Setup", std::move(setup));
    panels.emplace_back("Console",   std::make_unique<JPConsolePanel>(m_graph, *m_cell));
    panels.emplace_back("Axes",      std::make_unique<JPAxesPanel>(m_graph, *m_cell));

    const bool first = m_docks.empty();
    for (size_t i = 0; i < panels.size(); ++i) {
        if (first) {
            Dock d;
            d.dock = std::make_unique<JDockWidget>(panels[i].first, 0.f, 0.f, 0.f, 0.f);
            // Driving the machine on the right; what it says, along the bottom.
            JDockHost& area = i < kRightPanels ? m_window.dockSpace().right() : m_window.dockSpace().bottom();
            area.addDock(d.dock.get());
            m_docks.push_back(std::move(d));
        }
        m_docks[i].panel = std::move(panels[i].second);
        m_docks[i].dock->setContent(m_docks[i].panel.get());
    }
    if (first) {
        // Each area opens on its first panel (the last added would be in front).
        showDock("Machine");
        showDock("Console");
    }
}

void JPlacerMachine::park() {
    if (!m_cell) return;
    for (const JPHeadConfig& h : m_cell->config().heads)
        if (h.park) {
            JLOGC(JPlacerLog::kUi, JLogLevel::Info) << "Machine: park " << h.name;
            m_cell->park(h.id, kParkSpeed);
            return;
        }
    m_window.showStatus("No head has a park place set", kErrorMs);
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
}

bool JPlacerMachine::openCell(const std::string& path, std::string& error) {
    JPCellConfig config;
    if (!config.load(path, error)) return false;
    for (const std::string& p : config.problems()) JLOGC(JPlacerLog::kApp, JLogLevel::Warn) << path << ": " << p;

    dropPanels();
    m_cell = std::make_unique<JPCell>(std::move(config), m_profiles);
    m_cellPath = path;

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
    m_unwatch.push_back(m_cell->onConnection.connect([this, onMain](bool ok, std::string why) {
        onMain([this, ok, why] {
            const bool wasConnecting = m_connecting;
            m_connecting = false;
            m_connectFailed = !ok && wasConnecting;
            if (ok) m_lost.clear();
            else if (!wasConnecting && !why.empty()) m_lost = why;   // dropped while working
            // A camera on screen gets its light as soon as there is a
            // machine to switch it; without one, its panel says why it is dark.
            if (ok) for (CameraDock& c : m_cameras) if (!c.panel->camera().lightActuator().empty()) c.panel->setNote("");
            lightCameras();
            if (!why.empty()) m_window.showStatus(why, kErrorMs);
            else m_window.showStatus(ok ? m_cell->config().name + " connected" : m_cell->config().name + " disconnected", kStatusMs);
        });
    }));
    // A home by the switches is finished with the camera where the head homes visually.
    // A new calibration straightens the picture from then on.
    m_unwatch.push_back(m_cell->onCalibration.connect([this, onMain] {
        onMain([this] { for (CameraDock& c : m_cameras) c.panel->refreshStraightening(); });
    }));
    m_unwatch.push_back(m_cell->onHomed.connect([this, onMain](bool homed) {
        onMain([this, homed] {
            if (homed && m_cameraTasks) m_cameraTasks->visualHome();
        });
    }));
    m_unwatch.push_back(m_cell->onState.connect([onMain](std::string, std::string) { onMain([] {}); }));
    m_unwatch.push_back(m_cell->onMotion.connect([this, onMain](bool ok, std::string why) {
        onMain([this, ok, why] {
            if (m_cell->isHomed() || ok) m_homeFailed = false;
            if (!ok && !why.empty()) {
                if (!m_cell->isHomed()) m_homeFailed = true;
                m_window.showStatus(why, kErrorMs);
            }
        });
    }));
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

void JPlacerMachine::squareMachine(const JPMountConfig& mount, double xPerY) {
    JPSquarenessConfig q = m_cell->squareness();
    if (q.axisX.empty() || q.axisY.empty()) {
        q.axisX = mount.axisX;
        q.axisY = mount.axisY;
        // Unchanged at the homing mark: its coordinates, and visual homing, stay as they were.
        for (const JPHeadConfig& h : m_cell->config().heads)
            if (h.id == mount.headId && h.homingFiducial) q.atY = h.homingFiducial->y;
    }
    q.xPerY += xPerY;   // measured in coordinates already corrected by the old
    m_cell->setSquareness(q);
    JPCellConfig saved;
    std::string error;
    if (saved.load(m_cellPath, error)) {
        saved.squareness = q;
        saved.save(m_cellPath, error);
    }
    if (!error.empty()) {
        JLOGC(JPlacerLog::kApp, JLogLevel::Error) << error;
        m_window.showStatus("The squareness is in use but was not saved: " + error, kErrorMs);
        return;
    }
    m_window.showStatus("Squareness saved: home the machine again, then Calibrate and Locate Board", kErrorMs);
}

void JPlacerMachine::setPort(const std::string& driverId, const std::string& port) {
    JLOGC(JPlacerLog::kCell, JLogLevel::Info) << m_cellPath << ": controller " << driverId << " now on " << port;
    JPCellConfig config;
    std::string error;
    bool changed = false;
    if (config.load(m_cellPath, error)) {
        for (JPDriverConfig& d : config.drivers)
            if (d.id == driverId && d.link["port"].str() != port) {
                d.link["port"] = port;
                changed = true;
            }
        if (!changed) return;
        if (config.save(m_cellPath, error) && openCell(m_cellPath, error)) return;
    }
    JDialog::message("The port could not be changed", error);
}

void JPlacerMachine::applySetup(JPCellConfig cell) {
    if (!m_cell) return;
    // Measured while it was being set up: the cell's, not the copy's.
    for (JPCameraConfig& c : cell.cameras) c.calibrations = m_cell->cameraCalibrations(c.id);
    cell.squareness = m_cell->squareness();
    std::weak_ptr<bool> alive = m_alive;
    auto apply = [this, alive, cell] {
        if (const auto a = alive.lock(); !a || !*a) return;
        std::string error;
        if (!cell.save(m_cellPath, error) || !openCell(m_cellPath, error)) {
            JDialog::message("Machine Setup could not be applied", error);
            return;
        }
        JLOGC(JPlacerLog::kUi, JLogLevel::Info) << "Machine Setup applied to " << m_cellPath;
        m_window.showStatus("Machine Setup applied", kStatusMs);
    };
    if (!m_cell->isConnected()) {
        apply();
        return;
    }
    JDialog::confirm("Apply Machine Setup",
                     "The machine is opened again with the new setup: it disconnects, connects again, and must be "
                     "homed again before it moves.",
                     apply);
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

void JPlacerMachine::importFrom(const std::string& path) {
    JPCellConfig cell;
    std::vector<std::string> notes;
    std::string error;
    if (!JPOpenPnpMachineImporter::import(path, cell, notes, error)) {
        JDialog::message("The OpenPnP machine could not be imported", error);
        return;
    }
    const std::string target = (std::filesystem::path(cellsDir()) / kImportedCellFile).string();
    // Importing again keeps how each controller is reached where it was
    // already chosen here (the port picked by its permanent name): OpenPnP's
    // file still names the port it had, which may be a different device now.
    JPCellConfig previous;
    std::string ignored;
    std::error_code ec;
    if (std::filesystem::exists(target, ec) && previous.load(target, ignored)) {
        for (JPDriverConfig& d : cell.drivers)
            if (const JPDriverConfig* was = previous.driver(d.id); was && was->link["type"].str() == d.link["type"].str()) {
                JLOGC(JPlacerLog::kImport, JLogLevel::Info) << "controller " << d.name << " keeps " << was->link["port"].str();
                d.link = was->link;
            }
        // What jplacer measured itself is not OpenPnP's to replace: each
        // camera's calibration, and the squareness it measured.
        for (JPCameraConfig& cam : cell.cameras)
            for (const JPCameraConfig& was : previous.cameras)
                if (was.id == cam.id) cam.calibrations = was.calibrations;
        if (previous.squareness.active()) cell.squareness = previous.squareness;
    }
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
    JDialog::message("OpenPnP machine imported", body);
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

void JPlacerMachine::lightCameras() {
    if (!m_cell) return;
    const bool connected = m_cell->isConnected();
    // A light is on while any camera it lights runs.
    std::map<std::string, bool> lights;
    for (CameraDock& c : m_cameras) {
        const std::string light = c.panel->camera().lightActuator();
        if (light.empty()) continue;
        lights[light] = lights[light] || c.panel->isRunning();
        if (!connected) c.panel->setNote("Light off: connect the machine to light this camera.");
    }
    if (!connected) return;
    for (const auto& [light, on] : lights) m_cell->switchActuator(light, on);
}

bool JPlacerMachine::showDock(const std::string& title) {
    for (CameraDock& c : m_cameras)
        if (c.dock->title() == title) {
            bringForward(*c.panel);
            return true;
        }
    for (Dock& d : m_docks) {
        if (d.dock->title() != title) continue;
        // Re-inserting a dock where it already is makes it the active tab.
        if (JDockHost* host = d.dock->placedIn()) host->insertDock(d.dock.get(), host->findDock(d.dock.get()));
        return true;
    }
    return false;
}

} // inline namespace jf
