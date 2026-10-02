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

#include <j/config/Settings.h>
#include <j/core/Dialog.h>
#include <j/core/Log.h>
#include <j/core/MainThreadDispatcher.h>

#include <cstdlib>
#include <filesystem>

inline namespace jf {

namespace {

// The file an imported OpenPnP machine is written to, in cellsDir().
constexpr const char* kImportedCellFile = "openpnp.json";
// How long a status-bar message stays: a confirmation briefly, a failure long
// enough to read the reason.
constexpr int kStatusMs = 3000;
constexpr int kErrorMs  = 8000;

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
    m_cameraTasks.reset();   // a task under way finishes first: it drives the cell and a camera
    for (const auto& u : m_unwatch) u();
    m_unwatch.clear();
    for (Dock& d : m_docks) {
        d.dock->setContent(nullptr);
        d.panel.reset();
    }
    if (m_cameras) {
        m_window.setCentralWidget(nullptr);
        m_cameras.reset();
    }
}

void JPlacerMachine::buildPanels() {
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
    panels.emplace_back("Console",   std::make_unique<JPConsolePanel>(m_graph, *m_cell));
    panels.emplace_back("Axes",      std::make_unique<JPAxesPanel>(m_graph, *m_cell));

    const bool first = m_docks.empty();
    for (size_t i = 0; i < panels.size(); ++i) {
        if (first) {
            Dock d;
            d.dock = std::make_unique<JDockWidget>(panels[i].first, 0.f, 0.f, 0.f, 0.f);
            // Driving the machine on the right; what it says, along the bottom.
            JDockHost& area = i < 3 ? m_window.dockSpace().right() : m_window.dockSpace().bottom();
            area.addDock(d.dock.get());
            m_docks.push_back(std::move(d));
        }
        m_docks[i].panel = std::move(panels[i].second);
        m_docks[i].dock->setContent(m_docks[i].panel.get());
    }
    // The cameras fill the centre: what the machine sees is what the person
    // works from. A camera's light is on while it is the one shown.
    // A head camera looks where its axes put it, plus its offset on the head;
    // a fixed one is drawn with nothing under it.
    auto viewFor = [cell = m_cell.get()](const JPCameraConfig& c) -> std::function<bool(double&, double&)> {
        if (c.mount.axisX.empty() || c.mount.axisY.empty()) return nullptr;
        return [cell, m = c.mount](double& x, double& y) {
            const auto p = cell->positions();
            const auto px = p.find(m.axisX), py = p.find(m.axisY);
            if (px == p.end() || py == p.end()) return false;
            x = px->second + m.offsetX;
            y = py->second + m.offsetY;
            return true;
        };
    };
    m_cameras = std::make_unique<JPCameraPanel>(m_graph, m_window.hal(), m_cell->config(),
                                                (std::filesystem::path(JPlacerPaths::configDir()) / "captures").string(),
                                                viewFor);
    m_cameras->onShown = [this](const std::string& shown, const std::string& before) { lightCameras(shown, before); };
    m_window.setCentralWidget(m_cameras.get());
    lightCameras(m_cameras->shownId(), std::string());
    m_cameraTasks = std::make_unique<JPlacerCameraTasks>(m_window, *m_cell, *m_cameras, m_cellPath);

    if (first) {
        // Each area opens on its first panel (the last added would be in front).
        showDock("Machine");
        showDock("Console");
    }
}

std::string JPlacerMachine::cellsDir() {
    return (std::filesystem::path(JPlacerPaths::configDir()) / "cells").string();
}

void JPlacerMachine::setMenuItems(JMenuItem* connect, JMenuItem* disconnect, JMenuItem* home) {
    m_connectItem    = connect;
    m_disconnectItem = disconnect;
    m_homeItem       = home;
    updateMenu();
}

void JPlacerMachine::updateMenu() {
    const bool open = m_cell != nullptr, connected = open && m_cell->isConnected();
    if (m_connectItem)    m_connectItem->setEnabled(open && !connected);
    if (m_disconnectItem) m_disconnectItem->setEnabled(connected);
    if (m_homeItem)       m_homeItem->setEnabled(connected);
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
            // The camera on show gets its light as soon as there is a machine
            // to switch it; without one, the panel says why it is dark.
            if (m_cameras) lightCameras(m_cameras->shownId(), std::string());
            if (!why.empty()) m_window.showStatus(why, kErrorMs);
            else m_window.showStatus(ok ? m_cell->config().name + " connected" : m_cell->config().name + " disconnected", kStatusMs);
        });
    }));
    m_unwatch.push_back(m_cell->onHomed.connect([onMain](bool) { onMain([] {}); }));
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
    if (std::filesystem::exists(target, ec) && previous.load(target, ignored))
        for (JPDriverConfig& d : cell.drivers)
            if (const JPDriverConfig* was = previous.driver(d.id); was && was->link["type"].str() == d.link["type"].str()) {
                JLOGC(JPlacerLog::kImport, JLogLevel::Info) << "controller " << d.name << " keeps " << was->link["port"].str();
                d.link = was->link;
            }
    if (!cell.save(target, error) || !openCell(target, error)) {
        JDialog::message("The OpenPnP machine could not be imported", error);
        return;
    }
    std::string body = std::to_string(cell.drivers.size()) + " controller(s), " + std::to_string(cell.axes.size())
                     + " axes, " + std::to_string(cell.nozzles.size()) + " nozzle(s), "
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

void JPlacerMachine::lightCameras(const std::string& shown, const std::string& before) {
    if (!m_cell || !m_cameras) return;
    auto light = [this](const std::string& cameraId) -> std::string {
        for (const JPCameraConfig& c : m_cell->config().cameras)
            if (c.id == cameraId) return c.device["light-actuator-id"].str();
        return {};
    };
    const std::string on = light(shown);
    if (!m_cell->isConnected()) {
        m_cameras->setNote(on.empty() ? std::string() : "Light off: connect the machine to light this camera.");
        return;
    }
    m_cameras->setNote("");
    if (const std::string off = light(before); !off.empty() && off != on) m_cell->switchActuator(off, false);
    if (!on.empty()) m_cell->switchActuator(on, true);
}

bool JPlacerMachine::showDock(const std::string& title) {
    for (Dock& d : m_docks) {
        if (d.dock->title() != title) continue;
        // Re-inserting a dock where it already is makes it the active tab.
        if (JDockHost* host = d.dock->placedIn()) host->insertDock(d.dock.get(), host->findDock(d.dock.get()));
        return true;
    }
    return false;
}

} // inline namespace jf
