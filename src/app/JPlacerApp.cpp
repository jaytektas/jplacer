// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerApp.h"

#include "JPlacerLog.h"
#include "JPlacerMenuBuilder.h"
#include "JPlacerPreferencesDialog.h"
#include "JPlacerSettings.h"

#include <j/core/Dialog.h>
#include <j/core/Log.h>

inline namespace jf {

namespace {
constexpr const char* kWindowTitle  = "jplacer";
constexpr uint32_t    kWindowWidth  = 1280;
constexpr uint32_t    kWindowHeight = 800;
// Where jplacer's releases are published, and the variable that points it at a
// pretend one for testing (see JAppUpdater.h).
constexpr const char* kReleasesApi  = "https://api.github.com/repos/jaytektas/jplacer/releases/latest";
constexpr const char* kUpdateUrlEnv = "JPLACER_UPDATE_URL";
}

JPlacerApp::JPlacerApp(std::string settingsPath) {
    JPlacerSettings::load(settingsPath);
    JLOGC(JPlacerLog::kApp, JLogLevel::Info) << "starting jplacer " << JPLACER_VERSION;

    m_window = std::make_unique<JAppWindow>(kWindowTitle, kWindowWidth, kWindowHeight);
    if (!m_window->valid()) {
        JLOGC(JPlacerLog::kApp, JLogLevel::Error) << "window / GPU HAL init failed";
        return;
    }

    m_updater = std::make_unique<JAppUpdater>(
        *m_window, JAppUpdater::JConfig{ "jplacer", JPLACER_VERSION, kReleasesApi, kUpdateUrlEnv,
                                         JPlacerSettings::kUpdatesBeta });

    JPlacerMenuBuilder::build(*m_window, m_app.sceneGraph(), *this);
    m_window->setStatusText("jplacer " JPLACER_VERSION);
}

JPlacerApp::~JPlacerApp() {
    // The updater records "don't ask about this version again" in JSettings
    // without saving, so the file is written once more on the way out.
    JPlacerSettings::save();
    JLOGC(JPlacerLog::kApp, JLogLevel::Info) << "shut down";
}

bool JPlacerApp::valid() const { return m_window && m_window->valid(); }

int JPlacerApp::run() {
    if (!valid()) return -1;
    // Only a newer version is reported from here: no network on the shop floor is
    // not worth interrupting for every time jplacer opens.
    if (JPlacerSettings::updatesAtStartup()) m_updater->check(false);
    const int rc = m_window->run();
    m_updater->installStaged();
    return rc;
}

void JPlacerApp::openPreferences() {
    m_window->openModal<JPlacerPreferencesDialog>([this] { m_updater->check(true); });
}

void JPlacerApp::showAbout() {
    JDialog::message("About jplacer",
        "jplacer " JPLACER_VERSION "\n\n"
        "Pick-and-place machine control.\n\n"
        "Copyright (C) 2026 Jason Roughley\n"
        "This program is free software: you can redistribute it and/or modify it under the "
        "terms of the GNU General Public License, version 3 or later. It comes with ABSOLUTELY "
        "NO WARRANTY.\n\n"
        "https://github.com/jaytektas/jplacer");
}

} // inline namespace jf
