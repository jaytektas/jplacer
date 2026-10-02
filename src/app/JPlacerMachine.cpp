// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerMachine.h"

#include "JPlacerSettings.h"

#include "common/JPlacerLog.h"
#include "common/JPlacerPaths.h"
#include "openpnp/JPOpenPnpMachineImporter.h"

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
    if (m_unwatch) m_unwatch();
    if (m_dock) m_dock->setContent(nullptr);
    m_panel.reset();      // stops listening to the cell before the cell goes
    m_cell.reset();
}

std::string JPlacerMachine::cellsDir() {
    return (std::filesystem::path(JPlacerPaths::configDir()) / "cells").string();
}

void JPlacerMachine::setMenuItems(JMenuItem* connect, JMenuItem* disconnect) {
    m_connectItem    = connect;
    m_disconnectItem = disconnect;
    updateMenu();
}

void JPlacerMachine::updateMenu() {
    const bool open = m_cell != nullptr, connected = open && m_cell->isConnected();
    if (m_connectItem)    m_connectItem->setEnabled(open && !connected);
    if (m_disconnectItem) m_disconnectItem->setEnabled(connected);
}

bool JPlacerMachine::openCell(const std::string& path, std::string& error) {
    JPCellConfig config;
    if (!config.load(path, error)) return false;
    for (const std::string& p : config.problems()) JLOGC(JPlacerLog::kApp, JLogLevel::Warn) << path << ": " << p;

    if (m_unwatch) m_unwatch();
    if (m_dock) m_dock->setContent(nullptr);
    m_panel.reset();
    m_cell = std::make_unique<JPCell>(std::move(config), m_profiles);

    std::weak_ptr<bool> alive = m_alive;
    m_unwatch = m_cell->onConnection.connect([this, alive](bool, std::string) {
        JMainThreadDispatcher::instance().post([this, alive] {
            if (const auto a = alive.lock(); a && *a) updateMenu();
        });
    });
    m_panel = std::make_unique<JPMachinePanel>(m_graph, *m_cell);
    if (!m_dock) {
        m_dock = std::make_unique<JDockWidget>("Machine", 0.f, 0.f, 0.f, 0.f);
        m_window.dockSpace().right().addDock(m_dock.get());
    }
    m_dock->setContent(m_panel.get());

    JSettings::instance().set(JPlacerSettings::kMachineCell, path);
    JPlacerSettings::save();
    JLOGC(JPlacerLog::kApp, JLogLevel::Info) << "cell " << m_cell->config().name << " from " << path;
    updateMenu();
    return true;
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

} // inline namespace jf
