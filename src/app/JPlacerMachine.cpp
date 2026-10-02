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
// Where OpenPnP keeps its machine, under the home folder.
constexpr const char* kOpenPnpDir         = ".openpnp2";
constexpr const char* kOpenPnpMachineFile = "machine.xml";

} // namespace

JPlacerMachine::JPlacerMachine(JAppWindow& window, JSceneGraph& graph)
    : m_window(window), m_graph(graph), m_profiles(JPFirmwareProfile::loadAll()) {
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
    for (const auto& u : m_unwatch) u();
    m_unwatch.clear();
    for (Dock& d : m_docks) {
        d.dock->setContent(nullptr);
        d.panel.reset();
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
    if (first) {
        // Each area opens on its first panel (the last added would be in front).
        for (size_t i : { size_t(0), size_t(3) }) {
            JDockHost* host = m_docks[i].dock->placedIn();
            if (host) host->insertDock(m_docks[i].dock.get(), host->findDock(m_docks[i].dock.get()));
        }
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
    auto follow = [this, alive](std::string why) {
        JMainThreadDispatcher::instance().post([this, alive, why] {
            if (const auto a = alive.lock(); a && *a) {
                updateMenu();
                showNotice(why);
            }
        });
    };
    m_unwatch.push_back(m_cell->onConnection.connect([follow](bool, std::string why) { follow(why); }));
    m_unwatch.push_back(m_cell->onHomed.connect([follow](bool) { follow(std::string()); }));
    m_unwatch.push_back(m_cell->onState.connect([follow](std::string, std::string) { follow(std::string()); }));
    buildPanels();

    JSettings::instance().set(JPlacerSettings::kMachineCell, path);
    JPlacerSettings::save();
    JLOGC(JPlacerLog::kApp, JLogLevel::Info) << "cell " << m_cell->config().name << " from " << path;
    updateMenu();
    showNotice(std::string());
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

void JPlacerMachine::showNotice(const std::string& why) {
    if (!m_cell) {
        m_window.setNotice("");
    } else if (!m_cell->isConnected()) {
        std::string detail = m_cell->config().name + " is not connected: positions are the last ones received, "
                             "not live. Machine \xE2\x96\xB8 Connect.";
        if (!why.empty()) detail = why;
        m_window.setNotice("NOT CONNECTED", detail, Colors::Danger);
    } else if (m_cell->inAlarm()) {
        m_window.setNotice("ALARM", "A controller has stopped on an alarm (a limit switch, an emergency stop, "
                           "or a failed home). Find the cause; the Console shows what it said.", Colors::Danger);
    } else if (!m_cell->isHomed()) {
        m_window.setNotice("NOT HOMED", "Positions are not yet known, so the machine will not move. "
                           "Machine \xE2\x96\xB8 Home All Axes.", Colors::Warning);
    } else {
        m_window.setNotice("");
    }
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
    if (m_cell) m_cell->connect();
}

void JPlacerMachine::disconnect() {
    if (m_cell) m_cell->disconnect();
}

void JPlacerMachine::home() {
    if (m_cell) m_cell->home();
}

} // inline namespace jf
